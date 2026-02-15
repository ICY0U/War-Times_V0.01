#include "Renderer.h"
#include "Util/Log.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

namespace WT {

bool Renderer::Init(HWND hwnd, int width, int height) {
    m_width  = width;
    m_height = height;

    if (!CreateDeviceAndSwapChain(hwnd))  return false;
    QueryGPUInfo();
    if (!CreateRenderTargetView())        return false;
    if (!CreateDepthStencilView())        return false;
    if (!CreateMSAATargets())             return false;
    if (!CreateRasterizerStates())        return false;
    if (!CreateDepthStencilStates())      return false;
    if (!CreateBlendStates())             return false;
    if (!CreateSamplerStates())           return false;

    SetViewport();

    // Set default states
    m_context->RSSetState(m_rasterizerSolid.Get());
    m_context->OMSetDepthStencilState(m_depthEnabled.Get(), 0);

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blendOpaque.Get(), blendFactor, 0xFFFFFFFF);

    LOG_INFO("DirectX 11 Renderer initialized (%dx%d, MSAA: %ux)", width, height, m_sampleCount);
    return true;
}

void Renderer::Shutdown() {
    if (m_swapChain) {
        m_swapChain->SetFullscreenState(FALSE, nullptr);
    }
    m_context->ClearState();
    m_context->Flush();
    LOG_INFO("Renderer shutdown");
}

void Renderer::BeginFrame(float r, float g, float b, float a) {
    float clearColor[4] = { r, g, b, a };
    m_stats = {};  // Reset per-frame stats

    // Always restore the viewport (shadow pass may have changed it)
    SetViewport();

    if (m_sampleCount > 1 && m_msaaRTV) {
        // Render to MSAA targets
        m_context->ClearRenderTargetView(m_msaaRTV.Get(), clearColor);
        m_context->ClearDepthStencilView(m_msaaDSV.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        m_context->OMSetRenderTargets(1, m_msaaRTV.GetAddressOf(), m_msaaDSV.Get());
    } else {
        // Render directly to swap chain back buffer
        m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
        m_context->ClearDepthStencilView(m_depthStencilView.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        m_context->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
    }
}

void Renderer::EndFrame() {
    if (m_sampleCount > 1 && m_msaaColorBuffer && !m_skipMSAAResolve) {
        // Resolve MSAA render target to the swap chain back buffer
        m_context->ResolveSubresource(
            m_backBuffer.Get(), 0,
            m_msaaColorBuffer.Get(), 0,
            DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    m_swapChain->Present(m_vsync ? 1 : 0, 0);
}

void Renderer::OnResize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_width  = width;
    m_height = height;

    // Unbind render targets
    m_context->OMSetRenderTargets(0, nullptr, nullptr);

    // Release all target resources
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    m_depthSRV.Reset();
    m_depthStencilBuffer.Reset();
    m_backBuffer.Reset();
    m_msaaColorBuffer.Reset();
    m_msaaRTV.Reset();
    m_msaaDepthBuffer.Reset();
    m_msaaDSV.Reset();

    // Resize swap chain buffers
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to resize swap chain buffers");
        return;
    }

    CreateRenderTargetView();
    CreateDepthStencilView();
    CreateMSAATargets();
    SetViewport();

    LOG_INFO("Renderer resized to %dx%d", width, height);
}

void Renderer::SetWireframe(bool enable) {
    m_context->RSSetState(enable ? m_rasterizerWireframe.Get() : m_rasterizerSolid.Get());
}

void Renderer::SetDepthEnabled(bool enable) {
    m_context->OMSetDepthStencilState(
        enable ? m_depthEnabled.Get() : m_depthDisabled.Get(), 0);
}

void Renderer::SetAlphaBlending(bool enable) {
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(
        enable ? m_blendAlpha.Get() : m_blendOpaque.Get(),
        blendFactor, 0xFFFFFFFF);
}

void Renderer::SetMSAA(UINT sampleCount) {
    if (sampleCount == m_sampleCount) return;
    m_sampleCount = sampleCount;

    m_msaaColorBuffer.Reset();
    m_msaaRTV.Reset();
    m_msaaDepthBuffer.Reset();
    m_msaaDSV.Reset();

    if (m_sampleCount > 1) {
        CreateMSAATargets();
    }
    LOG_INFO("MSAA set to %ux", m_sampleCount);
}

void Renderer::TrackDrawCall(uint32_t indexCount) {
    m_stats.drawCalls++;
    m_stats.triangles += indexCount / 3;
}

// ========================================
// Private implementation
// ========================================

bool Renderer::CreateDeviceAndSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount       = 2;
    scd.BufferDesc.Width  = m_width;
    scd.BufferDesc.Height = m_height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator   = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow      = hwnd;
    scd.SampleDesc.Count  = 1;     // Swap chain is always non-MSAA with FLIP_DISCARD
    scd.SampleDesc.Quality = 0;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags             = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL featureLevelOut;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createFlags, featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION, &scd,
        m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
        &featureLevelOut, m_context.GetAddressOf());

    // Fallback: retry without debug layer if it's not available
    if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        LOG_WARN("D3D11 Debug layer not available, retrying without debug flag");
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            createFlags, featureLevels, _countof(featureLevels),
            D3D11_SDK_VERSION, &scd,
            m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
            &featureLevelOut, m_context.GetAddressOf());
    }

    HR_CHECK(hr, "D3D11CreateDeviceAndSwapChain");
    m_gpuInfo.featureLevel = featureLevelOut;
    LOG_INFO("D3D11 Device created (Feature Level %x)", static_cast<int>(featureLevelOut));
    return true;
}

void Renderer::QueryGPUInfo() {
    ComPtr<IDXGIDevice> dxgiDevice;
    m_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    if (dxgiDevice) {
        dxgiDevice->GetAdapter(adapter.GetAddressOf());
    }

    if (adapter) {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        m_gpuInfo.adapterName           = desc.Description;
        m_gpuInfo.dedicatedVideoMemoryMB = desc.DedicatedVideoMemory / (1024 * 1024);
        m_gpuInfo.sharedSystemMemoryMB   = desc.SharedSystemMemory   / (1024 * 1024);

        LOG_INFO("GPU: %ls", m_gpuInfo.adapterName.c_str());
        LOG_INFO("  VRAM: %zu MB  |  Shared: %zu MB",
                 m_gpuInfo.dedicatedVideoMemoryMB, m_gpuInfo.sharedSystemMemoryMB);
    }
}

bool Renderer::CreateRenderTargetView() {
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.GetAddressOf()));
    HR_CHECK(hr, "GetBuffer (back buffer)");

    hr = m_device->CreateRenderTargetView(m_backBuffer.Get(), nullptr, m_renderTargetView.GetAddressOf());
    HR_CHECK(hr, "CreateRenderTargetView");
    return true;
}

bool Renderer::CreateDepthStencilView() {
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width     = m_width;
    depthDesc.Height    = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format    = DXGI_FORMAT_R24G8_TYPELESS;  // Typeless for DSV + SRV
    depthDesc.SampleDesc.Count   = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage     = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_depthStencilBuffer.GetAddressOf());
    HR_CHECK(hr, "CreateTexture2D (depth buffer)");

    // Create DSV with depth format
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
    HR_CHECK(hr, "CreateDepthStencilView");

    // Create SRV for reading depth in shaders (SSAO etc.)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &srvDesc, m_depthSRV.GetAddressOf());
    HR_CHECK(hr, "CreateDepthSRV");

    return true;
}

bool Renderer::CreateMSAATargets() {
    if (m_sampleCount <= 1) return true;

    // Check MSAA quality levels
    UINT qualityLevels = 0;
    m_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, m_sampleCount, &qualityLevels);
    if (qualityLevels == 0) {
        LOG_WARN("MSAA %ux not supported, falling back to no MSAA", m_sampleCount);
        m_sampleCount = 1;
        return true;
    }

    UINT quality = qualityLevels - 1;

    // MSAA color buffer
    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width     = m_width;
    colorDesc.Height    = m_height;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count   = m_sampleCount;
    colorDesc.SampleDesc.Quality = quality;
    colorDesc.Usage     = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    HRESULT hr = m_device->CreateTexture2D(&colorDesc, nullptr, m_msaaColorBuffer.GetAddressOf());
    HR_CHECK(hr, "CreateTexture2D (MSAA color)");

    hr = m_device->CreateRenderTargetView(m_msaaColorBuffer.Get(), nullptr, m_msaaRTV.GetAddressOf());
    HR_CHECK(hr, "CreateRenderTargetView (MSAA)");

    // MSAA depth buffer
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width     = m_width;
    depthDesc.Height    = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count   = m_sampleCount;
    depthDesc.SampleDesc.Quality = quality;
    depthDesc.Usage     = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D(&depthDesc, nullptr, m_msaaDepthBuffer.GetAddressOf());
    HR_CHECK(hr, "CreateTexture2D (MSAA depth)");

    hr = m_device->CreateDepthStencilView(m_msaaDepthBuffer.Get(), nullptr, m_msaaDSV.GetAddressOf());
    HR_CHECK(hr, "CreateDepthStencilView (MSAA)");

    LOG_INFO("MSAA %ux enabled (quality level %u)", m_sampleCount, quality);
    return true;
}

void Renderer::SetViewport() {
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width    = static_cast<float>(m_width);
    vp.Height   = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

bool Renderer::CreateRasterizerStates() {
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;    // Enable MSAA rasterization
    rd.AntialiasedLineEnable = TRUE;
    HRESULT hr = m_device->CreateRasterizerState(&rd, m_rasterizerSolid.GetAddressOf());
    HR_CHECK(hr, "CreateRasterizerState (solid)");

    rd.FillMode = D3D11_FILL_WIREFRAME;
    rd.CullMode = D3D11_CULL_NONE;
    hr = m_device->CreateRasterizerState(&rd, m_rasterizerWireframe.GetAddressOf());
    HR_CHECK(hr, "CreateRasterizerState (wireframe)");

    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    hr = m_device->CreateRasterizerState(&rd, m_rasterizerNoCull.GetAddressOf());
    HR_CHECK(hr, "CreateRasterizerState (no-cull)");

    return true;
}

bool Renderer::CreateDepthStencilStates() {
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable    = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc      = D3D11_COMPARISON_LESS;
    HRESULT hr = m_device->CreateDepthStencilState(&dsd, m_depthEnabled.GetAddressOf());
    HR_CHECK(hr, "CreateDepthStencilState (enabled)");

    dsd.DepthEnable = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = m_device->CreateDepthStencilState(&dsd, m_depthDisabled.GetAddressOf());
    HR_CHECK(hr, "CreateDepthStencilState (disabled)");

    return true;
}

bool Renderer::CreateBlendStates() {
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable    = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = m_device->CreateBlendState(&bd, m_blendOpaque.GetAddressOf());
    HR_CHECK(hr, "CreateBlendState (opaque)");

    bd.RenderTarget[0].BlendEnable           = TRUE;
    bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_device->CreateBlendState(&bd, m_blendAlpha.GetAddressOf());
    HR_CHECK(hr, "CreateBlendState (alpha)");

    return true;
}

bool Renderer::CreateSamplerStates() {
    // s0 — Point sampling (pixelated voxel textures)
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxAnisotropy  = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = m_device->CreateSamplerState(&sd, m_samplerPoint.GetAddressOf());
    HR_CHECK(hr, "CreateSamplerState (point)");

    // s1 — Bilinear
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = m_device->CreateSamplerState(&sd, m_samplerLinear.GetAddressOf());
    HR_CHECK(hr, "CreateSamplerState (linear)");

    // s2 — Anisotropic 16x
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 16;
    hr = m_device->CreateSamplerState(&sd, m_samplerAniso.GetAddressOf());
    HR_CHECK(hr, "CreateSamplerState (aniso)");

    // Bind all three to PS slots 0, 1, 2
    ID3D11SamplerState* samplers[] = {
        m_samplerPoint.Get(), m_samplerLinear.Get(), m_samplerAniso.Get()
    };
    m_context->PSSetSamplers(0, 3, samplers);

    LOG_INFO("Sampler states created (point / linear / aniso-16x)");
    return true;
}

} // namespace WT
