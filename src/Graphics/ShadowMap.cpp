#include "ShadowMap.h"
#include "Util/Log.h"

namespace WT {

bool ShadowMap::Init(ID3D11Device* device, UINT resolution) {
    m_resolution = resolution;

    // Create depth texture with both DSV and SRV bind flags
    // Use TYPELESS format so we can create DSV (DEPTH) and SRV (FLOAT) views
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = m_resolution;
    texDesc.Height    = m_resolution;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count   = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, m_depthTexture.GetAddressOf());
    HR_CHECK(hr, "ShadowMap CreateTexture2D");

    // Depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = device->CreateDepthStencilView(m_depthTexture.Get(), &dsvDesc, m_dsv.GetAddressOf());
    HR_CHECK(hr, "ShadowMap CreateDSV");

    // Shader resource view (for sampling in main pass)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    hr = device->CreateShaderResourceView(m_depthTexture.Get(), &srvDesc, m_srv.GetAddressOf());
    HR_CHECK(hr, "ShadowMap CreateSRV");

    // Comparison sampler for hardware PCF
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter   = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.BorderColor[0] = 1.0f;  // Border = max depth (no shadow outside map)
    sampDesc.BorderColor[1] = 1.0f;
    sampDesc.BorderColor[2] = 1.0f;
    sampDesc.BorderColor[3] = 1.0f;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&sampDesc, m_comparisonSampler.GetAddressOf());
    HR_CHECK(hr, "ShadowMap CreateComparisonSampler");

    // Shadow map viewport
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width    = static_cast<float>(m_resolution);
    m_viewport.Height   = static_cast<float>(m_resolution);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;

    LOG_INFO("Shadow map created (%ux%u)", m_resolution, m_resolution);
    return true;
}

void ShadowMap::Shutdown() {
    m_depthTexture.Reset();
    m_dsv.Reset();
    m_srv.Reset();
    m_comparisonSampler.Reset();
}

void ShadowMap::BeginShadowPass(ID3D11DeviceContext* ctx) {
    // Set viewport to shadow map resolution
    ctx->RSSetViewports(1, &m_viewport);

    // Clear depth
    ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Bind depth-only (no color render target)
    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(1, &nullRTV, m_dsv.Get());
}

void ShadowMap::EndShadowPass(ID3D11DeviceContext* ctx) {
    // Unbind render target
    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
}

void ShadowMap::BindSRV(ID3D11DeviceContext* ctx, UINT slot) const {
    ID3D11ShaderResourceView* srv = m_srv.Get();
    ctx->PSSetShaderResources(slot, 1, &srv);

    // Bind comparison sampler to slot 3
    ID3D11SamplerState* sampler = m_comparisonSampler.Get();
    ctx->PSSetSamplers(3, 1, &sampler);
}

void ShadowMap::UnbindSRV(ID3D11DeviceContext* ctx, UINT slot) const {
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(slot, 1, &nullSRV);
}

XMMATRIX ShadowMap::CalcLightViewProjection(XMFLOAT3 lightDir, XMFLOAT3 sceneCenter,
                                             float sceneRadius) const {
    XMVECTOR dir    = XMVector3Normalize(XMLoadFloat3(&lightDir));
    XMVECTOR center = XMLoadFloat3(&sceneCenter);
    XMVECTOR up     = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // If light is nearly vertical, use a different up vector
    float dotUp = fabsf(XMVectorGetX(XMVector3Dot(dir, up)));
    if (dotUp > 0.99f) {
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    // Place light camera behind scene along light direction
    XMVECTOR lightPos = center - dir * sceneRadius * 2.0f;

    XMMATRIX lightView = XMMatrixLookToLH(lightPos, dir, up);

    // Orthographic projection covering the scene
    float orthoSize = sceneRadius;
    XMMATRIX lightProj = XMMatrixOrthographicLH(
        orthoSize * 2.0f, orthoSize * 2.0f,
        0.1f, sceneRadius * 4.0f);

    return lightView * lightProj;
}

} // namespace WT
