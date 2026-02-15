#include "Shader.h"
#include "Util/Log.h"

namespace WT {

bool Shader::LoadVS(ID3D11Device* device, const std::wstring& path, const char* entryPoint,
                    const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount) {
    auto blob = CompileShader(path, entryPoint, "vs_5_0");
    if (!blob) return false;

    HRESULT hr = device->CreateVertexShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        m_vertexShader.GetAddressOf());
    HR_CHECK(hr, "CreateVertexShader");

    hr = device->CreateInputLayout(
        layout, layoutCount,
        blob->GetBufferPointer(), blob->GetBufferSize(),
        m_inputLayout.GetAddressOf());
    HR_CHECK(hr, "CreateInputLayout");

    // Store parameters for hot-reload
    m_vsPath  = path;
    m_vsEntry = entryPoint;
    m_vsLastWrite = GetFileWriteTime(path);
    StoreLayoutDesc(layout, layoutCount);

    return true;
}

bool Shader::LoadPS(ID3D11Device* device, const std::wstring& path, const char* entryPoint) {
    auto blob = CompileShader(path, entryPoint, "ps_5_0");
    if (!blob) return false;

    HRESULT hr = device->CreatePixelShader(
        blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
        m_pixelShader.GetAddressOf());
    HR_CHECK(hr, "CreatePixelShader");

    // Store parameters for hot-reload
    m_psPath  = path;
    m_psEntry = entryPoint;
    m_psLastWrite = GetFileWriteTime(path);

    return true;
}

void Shader::Bind(ID3D11DeviceContext* context) const {
    context->IASetInputLayout(m_inputLayout.Get());
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
}

// ---- Hot-Reload ----

void Shader::StoreLayoutDesc(const D3D11_INPUT_ELEMENT_DESC* layout, UINT count) {
    m_semanticNames.clear();
    m_layoutDesc.clear();

    // Copy semantic names into owned strings
    for (UINT i = 0; i < count; i++) {
        m_semanticNames.push_back(layout[i].SemanticName);
        m_layoutDesc.push_back(layout[i]);
    }
    // Fix up pointers to owned string data
    for (UINT i = 0; i < count; i++) {
        m_layoutDesc[i].SemanticName = m_semanticNames[i].c_str();
    }
}

FILETIME Shader::GetFileWriteTime(const std::wstring& path) const {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        return fad.ftLastWriteTime;
    }
    return {};
}

bool Shader::HasFileChanged() const {
    if (m_vsPath.empty() && m_psPath.empty()) return false;

    if (!m_vsPath.empty()) {
        FILETIME ft = GetFileWriteTime(m_vsPath);
        if (CompareFileTime(&ft, &m_vsLastWrite) != 0) return true;
    }
    if (!m_psPath.empty()) {
        FILETIME ft = GetFileWriteTime(m_psPath);
        if (CompareFileTime(&ft, &m_psLastWrite) != 0) return true;
    }
    return false;
}

bool Shader::Reload(ID3D11Device* device) {
    if (m_vsPath.empty() || m_psPath.empty()) return false;

    LOG_INFO("Hot-reloading shaders...");

    // Try to compile both before replacing anything
    auto vsBlob = CompileShader(m_vsPath, m_vsEntry.c_str(), "vs_5_0");
    if (!vsBlob) {
        LOG_ERROR("Hot-reload: vertex shader compilation failed");
        return false;
    }
    auto psBlob = CompileShader(m_psPath, m_psEntry.c_str(), "ps_5_0");
    if (!psBlob) {
        LOG_ERROR("Hot-reload: pixel shader compilation failed");
        return false;
    }

    // Both compiled — swap in new shaders
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_inputLayout.Reset();

    HRESULT hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
        m_vertexShader.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("Hot-reload: CreateVertexShader failed"); return false; }

    hr = device->CreateInputLayout(
        m_layoutDesc.data(), static_cast<UINT>(m_layoutDesc.size()),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("Hot-reload: CreateInputLayout failed"); return false; }

    hr = device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
        m_pixelShader.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("Hot-reload: CreatePixelShader failed"); return false; }

    m_vsLastWrite = GetFileWriteTime(m_vsPath);
    m_psLastWrite = GetFileWriteTime(m_psPath);

    LOG_INFO("Shaders reloaded successfully");
    return true;
}

// ---- Compilation ----

ComPtr<ID3DBlob> Shader::CompileShader(const std::wstring& path, const char* entryPoint, const char* target) {
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        path.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        target,
        compileFlags,
        0,
        shaderBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            LOG_ERROR("Shader compile error: %s", static_cast<const char*>(errorBlob->GetBufferPointer()));
        } else {
            LOG_ERROR("Failed to compile shader (file not found?)");
        }
        return nullptr;
    }

    if (errorBlob) {
        LOG_WARN("Shader compile warning: %s", static_cast<const char*>(errorBlob->GetBufferPointer()));
    }

    return shaderBlob;
}

} // namespace WT
