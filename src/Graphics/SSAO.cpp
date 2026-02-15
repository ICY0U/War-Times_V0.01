#include "SSAO.h"
#include "Util/Log.h"
#include <d3dcompiler.h>
#include <random>
#include <algorithm>

namespace WT {

// CB layout for SSAO — must match SSAOPS.hlsl (b6)
struct CBSSAO {
    XMFLOAT4X4 Projection;
    XMFLOAT4X4 InvProjection;
    XMFLOAT4X4 View;
    XMFLOAT4   Samples[64];    // Hemisphere kernel
    XMFLOAT2   ScreenSize;
    XMFLOAT2   NoiseScale;
    float      Radius;
    float      Bias;
    float      Intensity;
    int        KernelSize;
    float      NearZ;
    float      FarZ;
    XMFLOAT2   _pad;
};

static ComPtr<ID3DBlob> CompileShaderBlob(const std::wstring& path, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                     entry, target, flags, 0,
                                     blob.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LOG_ERROR("SSAO shader compile: %s", (char*)errors->GetBufferPointer());
        return nullptr;
    }
    return blob;
}

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void SSAO::GenerateKernel() {
    std::mt19937 rng(42);  // Fixed seed for determinism
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    m_kernel.resize(64);
    for (int i = 0; i < 64; i++) {
        // Random point in hemisphere (z > 0)
        XMFLOAT4 sample = {
            dist(rng),
            dist(rng),
            dist01(rng),   // Hemisphere — only positive z
            0.0f
        };

        // Normalize
        XMVECTOR v = XMLoadFloat4(&sample);
        v = XMVector3Normalize(v);

        // Random length (bias toward center)
        float scale = static_cast<float>(i) / 64.0f;
        scale = Lerp(0.1f, 1.0f, scale * scale);
        v = XMVectorScale(v, scale * dist01(rng));

        XMStoreFloat4(&m_kernel[i], v);
    }
}

bool SSAO::Init(ID3D11Device* device, int width, int height,
                const std::wstring& shaderDir) {
    m_width = width;
    m_height = height;

    GenerateKernel();

    if (!CreateTargets(device, width, height)) return false;
    if (!CreateNoiseTexture(device)) return false;

    // Compile shaders — reuse the PostProcess VS (fullscreen triangle)
    auto vsBlob = CompileShaderBlob(shaderDir + L"PostProcessVS.hlsl", "VSMain", "vs_5_0");
    if (!vsBlob) return false;
    HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                             nullptr, m_fullscreenVS.GetAddressOf());
    HR_CHECK(hr, "SSAO CreateVS");

    auto ssaoBlob = CompileShaderBlob(shaderDir + L"SSAOPS.hlsl", "SSAOMain", "ps_5_0");
    if (!ssaoBlob) return false;
    hr = device->CreatePixelShader(ssaoBlob->GetBufferPointer(), ssaoBlob->GetBufferSize(),
                                    nullptr, m_ssaoPS.GetAddressOf());
    HR_CHECK(hr, "SSAO CreatePS");

    auto blurBlob = CompileShaderBlob(shaderDir + L"SSAOPS.hlsl", "BlurMain", "ps_5_0");
    if (!blurBlob) return false;
    hr = device->CreatePixelShader(blurBlob->GetBufferPointer(), blurBlob->GetBufferSize(),
                                    nullptr, m_blurPS.GetAddressOf());
    HR_CHECK(hr, "SSAO Blur CreatePS");

    // Create constant buffer
    D3D11_BUFFER_DESC bd = {};
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth      = sizeof(CBSSAO);
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&bd, nullptr, m_ssaoCB.GetAddressOf());
    HR_CHECK(hr, "SSAO CreateCB");

    LOG_INFO("SSAO initialized (%dx%d, kernel=%d)", width, height, 64);
    return true;
}

void SSAO::Shutdown() {
    m_aoTexture.Reset(); m_aoRTV.Reset(); m_aoSRV.Reset();
    m_aoBlurTexture.Reset(); m_aoBlurRTV.Reset(); m_aoBlurSRV.Reset();
    m_noiseTexture.Reset(); m_noiseSRV.Reset();
    m_fullscreenVS.Reset(); m_ssaoPS.Reset(); m_blurPS.Reset();
    m_ssaoCB.Reset();
}

void SSAO::OnResize(ID3D11Device* device, int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_width = width;
    m_height = height;

    m_aoTexture.Reset(); m_aoRTV.Reset(); m_aoSRV.Reset();
    m_aoBlurTexture.Reset(); m_aoBlurRTV.Reset(); m_aoBlurSRV.Reset();

    CreateTargets(device, width, height);
}

bool SSAO::CreateTargets(ID3D11Device* device, int width, int height) {
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = width;
    texDesc.Height    = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R8_UNORM;  // Single channel AO
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr;

    // Raw AO
    hr = device->CreateTexture2D(&texDesc, nullptr, m_aoTexture.GetAddressOf());
    HR_CHECK(hr, "SSAO AO texture");
    hr = device->CreateRenderTargetView(m_aoTexture.Get(), nullptr, m_aoRTV.GetAddressOf());
    HR_CHECK(hr, "SSAO AO RTV");
    hr = device->CreateShaderResourceView(m_aoTexture.Get(), nullptr, m_aoSRV.GetAddressOf());
    HR_CHECK(hr, "SSAO AO SRV");

    // Blurred AO
    hr = device->CreateTexture2D(&texDesc, nullptr, m_aoBlurTexture.GetAddressOf());
    HR_CHECK(hr, "SSAO Blur texture");
    hr = device->CreateRenderTargetView(m_aoBlurTexture.Get(), nullptr, m_aoBlurRTV.GetAddressOf());
    HR_CHECK(hr, "SSAO Blur RTV");
    hr = device->CreateShaderResourceView(m_aoBlurTexture.Get(), nullptr, m_aoBlurSRV.GetAddressOf());
    HR_CHECK(hr, "SSAO Blur SRV");

    return true;
}

bool SSAO::CreateNoiseTexture(ID3D11Device* device) {
    // 4x4 random rotation vectors in tangent space
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    // Use R32G32B32A32_FLOAT for the noise (we only need 2 components but 4 is safest)
    float noiseData[4 * 4 * 4];  // 4x4 pixels * 4 floats
    for (int i = 0; i < 16; i++) {
        noiseData[i * 4 + 0] = dist(rng);
        noiseData[i * 4 + 1] = dist(rng);
        noiseData[i * 4 + 2] = 0.0f;       // z=0 (tangent space rotation around z)
        noiseData[i * 4 + 3] = 0.0f;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = 4;
    texDesc.Height    = 4;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = noiseData;
    initData.SysMemPitch = 4 * 4 * sizeof(float);

    HRESULT hr = device->CreateTexture2D(&texDesc, &initData, m_noiseTexture.GetAddressOf());
    HR_CHECK(hr, "SSAO noise texture");
    hr = device->CreateShaderResourceView(m_noiseTexture.Get(), nullptr, m_noiseSRV.GetAddressOf());
    HR_CHECK(hr, "SSAO noise SRV");

    return true;
}

void SSAO::DrawFullscreenTriangle(ID3D11DeviceContext* ctx) {
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->Draw(3, 0);
}

void SSAO::Compute(ID3D11DeviceContext* ctx,
                   ID3D11ShaderResourceView* depthSRV,
                   const XMMATRIX& projection,
                   const XMMATRIX& view,
                   float nearZ, float farZ,
                   const SSAOSettings& settings) {
    // Update constant buffer
    CBSSAO cbData = {};
    XMStoreFloat4x4(&cbData.Projection, XMMatrixTranspose(projection));

    XMVECTOR det = XMMatrixDeterminant(projection);
    XMMATRIX invProj = XMMatrixInverse(&det, projection);
    XMStoreFloat4x4(&cbData.InvProjection, XMMatrixTranspose(invProj));

    XMStoreFloat4x4(&cbData.View, XMMatrixTranspose(view));

    int ks = (std::min)(settings.kernelSize, 64);
    for (int i = 0; i < ks; i++)
        cbData.Samples[i] = m_kernel[i];

    cbData.ScreenSize = { static_cast<float>(m_width), static_cast<float>(m_height) };
    cbData.NoiseScale = { static_cast<float>(m_width) / 4.0f, static_cast<float>(m_height) / 4.0f };
    cbData.Radius     = settings.radius;
    cbData.Bias       = settings.bias;
    cbData.Intensity  = settings.intensity;
    cbData.KernelSize = ks;
    cbData.NearZ      = nearZ;
    cbData.FarZ       = farZ;

    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(m_ssaoCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cbData, sizeof(cbData));
    ctx->Unmap(m_ssaoCB.Get(), 0);

    // Set viewport to full resolution
    D3D11_VIEWPORT vp = {};
    vp.Width  = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    ctx->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(6, 1, m_ssaoCB.GetAddressOf());

    // ---- Pass 1: SSAO computation ----
    ctx->OMSetRenderTargets(1, m_aoRTV.GetAddressOf(), nullptr);
    float clearWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    ctx->ClearRenderTargetView(m_aoRTV.Get(), clearWhite);

    // Bind depth and noise textures
    ID3D11ShaderResourceView* srvs[2] = { depthSRV, m_noiseSRV.Get() };
    ctx->PSSetShaderResources(2, 2, srvs);  // t2 = depth, t3 = noise

    ctx->PSSetShader(m_ssaoPS.Get(), nullptr, 0);
    DrawFullscreenTriangle(ctx);

    // Unbind depth SRV
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(2, 2, nullSRVs);

    // ---- Pass 2: Blur ----
    ctx->OMSetRenderTargets(1, m_aoBlurRTV.GetAddressOf(), nullptr);
    ctx->ClearRenderTargetView(m_aoBlurRTV.Get(), clearWhite);

    ID3D11ShaderResourceView* aoSRV = m_aoSRV.Get();
    ctx->PSSetShaderResources(2, 1, &aoSRV);  // t2 = raw AO

    ctx->PSSetShader(m_blurPS.Get(), nullptr, 0);
    DrawFullscreenTriangle(ctx);

    // Unbind
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(2, 1, &nullSRV);
}

void SSAO::Unbind(ID3D11DeviceContext* ctx) {
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(4, 1, &nullSRV);  // t4 = AO texture
}

} // namespace WT
