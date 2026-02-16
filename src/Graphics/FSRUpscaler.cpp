#include "FSRUpscaler.h"
#include "Util/Log.h"
#include <d3dcompiler.h>

namespace WT {

static ComPtr<ID3DBlob> CompileFSRShader(const std::wstring& path, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                     entry, target, flags, 0,
                                     blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LOG_ERROR("FSR shader compile error: %s", (char*)errors->GetBufferPointer());
        return nullptr;
    }
    return blob;
}

bool FSRUpscaler::Init(ID3D11Device* device, int outputWidth, int outputHeight,
                        const std::wstring& shaderDir) {
    m_outputWidth  = outputWidth;
    m_outputHeight = outputHeight;

    // Compile VS (reuse the same fullscreen triangle approach as PostProcess)
    auto vsBlob = CompileFSRShader(shaderDir + L"PostProcessVS.hlsl", "VSMain", "vs_5_0");
    if (!vsBlob) return false;
    HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                             nullptr, m_fullscreenVS.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create VS"); return false; }

    // Compile EASU pixel shader
    auto easuBlob = CompileFSRShader(shaderDir + L"FSRPS.hlsl", "EASUMain", "ps_5_0");
    if (!easuBlob) return false;
    hr = device->CreatePixelShader(easuBlob->GetBufferPointer(), easuBlob->GetBufferSize(),
                                    nullptr, m_easuPS.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create EASU PS"); return false; }

    // Compile RCAS pixel shader
    auto rcasBlob = CompileFSRShader(shaderDir + L"FSRPS.hlsl", "RCASMain", "ps_5_0");
    if (!rcasBlob) return false;
    hr = device->CreatePixelShader(rcasBlob->GetBufferPointer(), rcasBlob->GetBufferSize(),
                                    nullptr, m_rcasPS.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create RCAS PS"); return false; }

    // Create constant buffer
    D3D11_BUFFER_DESC bd = {};
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth      = sizeof(CBFSRParams);
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&bd, nullptr, m_fsrCB.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create CB"); return false; }

    // Create linear sampler (for EASU texture sampling)
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, m_linearSampler.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create sampler"); return false; }

    // Create upscaled target at output resolution
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width     = outputWidth;
        texDesc.Height    = outputHeight;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage     = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        hr = device->CreateTexture2D(&texDesc, nullptr, m_upscaledTexture.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create upscaled texture"); return false; }

        hr = device->CreateRenderTargetView(m_upscaledTexture.Get(), nullptr, m_upscaledRTV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create upscaled RTV"); return false; }

        hr = device->CreateShaderResourceView(m_upscaledTexture.Get(), nullptr, m_upscaledSRV.GetAddressOf());
        if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create upscaled SRV"); return false; }
    }

    LOG_INFO("FSR: Initialized (%dx%d output)", outputWidth, outputHeight);
    return true;
}

void FSRUpscaler::Shutdown() {
    m_renderTexture.Reset();
    m_renderRTV.Reset();
    m_renderSRV.Reset();
    m_upscaledTexture.Reset();
    m_upscaledRTV.Reset();
    m_upscaledSRV.Reset();
    m_fullscreenVS.Reset();
    m_easuPS.Reset();
    m_rcasPS.Reset();
    m_fsrCB.Reset();
    m_linearSampler.Reset();
}

void FSRUpscaler::OnResize(ID3D11Device* device, int outputWidth, int outputHeight) {
    m_outputWidth  = outputWidth;
    m_outputHeight = outputHeight;

    // Recreate upscaled target
    m_upscaledTexture.Reset();
    m_upscaledRTV.Reset();
    m_upscaledSRV.Reset();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = outputWidth;
    texDesc.Height    = outputHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    device->CreateTexture2D(&texDesc, nullptr, m_upscaledTexture.GetAddressOf());
    device->CreateRenderTargetView(m_upscaledTexture.Get(), nullptr, m_upscaledRTV.GetAddressOf());
    device->CreateShaderResourceView(m_upscaledTexture.Get(), nullptr, m_upscaledSRV.GetAddressOf());
}

void FSRUpscaler::GetRenderResolution(int outputWidth, int outputHeight, FSRQuality quality,
                                       int& outRenderW, int& outRenderH) const {
    float scale = FSRQualityScale(quality);
    outRenderW = (std::max)(1, static_cast<int>(outputWidth  * scale));
    outRenderH = (std::max)(1, static_cast<int>(outputHeight * scale));
    // Ensure even dimensions
    outRenderW = (outRenderW + 1) & ~1;
    outRenderH = (outRenderH + 1) & ~1;
}

void FSRUpscaler::UpdateRenderTarget(ID3D11Device* device, int renderWidth, int renderHeight) {
    if (renderWidth == m_renderWidth && renderHeight == m_renderHeight && m_renderTexture)
        return;

    m_renderWidth  = renderWidth;
    m_renderHeight = renderHeight;

    m_renderTexture.Reset();
    m_renderRTV.Reset();
    m_renderSRV.Reset();

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = renderWidth;
    texDesc.Height    = renderHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr;
    hr = device->CreateTexture2D(&texDesc, nullptr, m_renderTexture.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create render texture"); return; }

    hr = device->CreateRenderTargetView(m_renderTexture.Get(), nullptr, m_renderRTV.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create render RTV"); return; }

    hr = device->CreateShaderResourceView(m_renderTexture.Get(), nullptr, m_renderSRV.GetAddressOf());
    if (FAILED(hr)) { LOG_ERROR("FSR: Failed to create render SRV"); return; }

    LOG_INFO("FSR: Render target updated to %dx%d", renderWidth, renderHeight);
}

void FSRUpscaler::Apply(ID3D11DeviceContext* ctx,
                         ID3D11RenderTargetView* outputRTV,
                         int outputWidth, int outputHeight,
                         float sharpness) {
    if (!m_renderSRV || !m_easuPS || !m_rcasPS) return;

    // Setup shared state
    ctx->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(nullptr);

    // Bind FSR linear sampler at slot 5 (avoid conflicting with engine samplers 0-3)
    ctx->PSSetSamplers(5, 1, m_linearSampler.GetAddressOf());

    D3D11_MAPPED_SUBRESOURCE mapped;

    // ---- PASS 1: EASU — Edge-Adaptive Spatial Upsampling ----
    // Input: render-res texture → Output: upscaled texture at output res
    {
        CBFSRParams params = {};
        params.InputSizeX  = static_cast<float>(m_renderWidth);
        params.InputSizeY  = static_cast<float>(m_renderHeight);
        params.OutputSizeX = static_cast<float>(outputWidth);
        params.OutputSizeY = static_cast<float>(outputHeight);
        params.PassMode    = 0; // EASU
        params.RcasSharpness = sharpness;

        ctx->Map(m_fsrCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &params, sizeof(params));
        ctx->Unmap(m_fsrCB.Get(), 0);
        ctx->PSSetConstantBuffers(6, 1, m_fsrCB.GetAddressOf());

        // Set render target to upscaled (output res)
        D3D11_VIEWPORT vp = {};
        vp.Width  = static_cast<float>(outputWidth);
        vp.Height = static_cast<float>(outputHeight);
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        ctx->OMSetRenderTargets(1, m_upscaledRTV.GetAddressOf(), nullptr);

        // Bind render-res source
        ID3D11ShaderResourceView* srv = m_renderSRV.Get();
        ctx->PSSetShaderResources(0, 1, &srv);

        ctx->PSSetShader(m_easuPS.Get(), nullptr, 0);
        DrawFullscreenTriangle(ctx);

        // Unbind input
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);
    }

    // ---- PASS 2: RCAS — Robust Contrast-Adaptive Sharpening ----
    // Input: upscaled texture → Output: final back buffer
    {
        CBFSRParams params = {};
        params.InputSizeX  = static_cast<float>(outputWidth);
        params.InputSizeY  = static_cast<float>(outputHeight);
        params.OutputSizeX = static_cast<float>(outputWidth);
        params.OutputSizeY = static_cast<float>(outputHeight);
        params.PassMode    = 1; // RCAS
        params.RcasSharpness = sharpness;

        ctx->Map(m_fsrCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &params, sizeof(params));
        ctx->Unmap(m_fsrCB.Get(), 0);

        ctx->OMSetRenderTargets(1, &outputRTV, nullptr);

        // Bind upscaled source
        ID3D11ShaderResourceView* srv = m_upscaledSRV.Get();
        ctx->PSSetShaderResources(0, 1, &srv);

        ctx->PSSetShader(m_rcasPS.Get(), nullptr, 0);
        DrawFullscreenTriangle(ctx);

        // Unbind input
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);
    }
}

void FSRUpscaler::DrawFullscreenTriangle(ID3D11DeviceContext* ctx) {
    ctx->Draw(3, 0);
}

} // namespace WT
