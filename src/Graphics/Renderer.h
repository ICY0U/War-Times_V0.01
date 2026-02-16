#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <cstdint>

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ---- Render Statistics (reset each frame) ----
struct RenderStats {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
};

// ---- GPU Adapter Info ----
struct GPUInfo {
    std::wstring adapterName;
    size_t dedicatedVideoMemoryMB = 0;
    size_t sharedSystemMemoryMB   = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
};

class Renderer {
public:
    bool Init(HWND hwnd, int width, int height);
    void Shutdown();

    void BeginFrame(float r = 0.4f, float g = 0.6f, float b = 0.9f, float a = 1.0f);
    void EndFrame();
    void OnResize(int width, int height);

    // Skip MSAA resolve in EndFrame (when post-processing handles output)
    void SetSkipMSAAResolve(bool skip) { m_skipMSAAResolve = skip; }

    // Accessors
    ID3D11Device*           GetDevice()  const { return m_device.Get(); }
    ID3D11DeviceContext*    GetContext() const { return m_context.Get(); }
    int   GetWidth()  const { return m_width; }
    int   GetHeight() const { return m_height; }
    float GetAspectRatio() const { return static_cast<float>(m_width) / static_cast<float>(m_height); }

    // Render state helpers
    void SetWireframe(bool enable);
    void SetDepthEnabled(bool enable);
    void SetAlphaBlending(bool enable);

    // Rasterizer state accessors
    ID3D11RasterizerState* GetSolidState()  const { return m_rasterizerSolid.Get(); }
    ID3D11RasterizerState* GetNoCullState() const { return m_rasterizerNoCull.Get(); }

    // VSync
    void SetVSync(bool enable) { m_vsync = enable; }
    bool IsVSync() const { return m_vsync; }

    // MSAA
    void SetMSAA(UINT sampleCount);
    UINT GetMSAASamples() const { return m_sampleCount; }

    // Render target accessors (for post-processing)
    ID3D11RenderTargetView* GetCurrentRTV() const {
        return (m_sampleCount > 1 && m_msaaRTV) ? m_msaaRTV.Get() : m_renderTargetView.Get();
    }
    ID3D11DepthStencilView* GetCurrentDSV() const {
        return (m_sampleCount > 1 && m_msaaDSV) ? m_msaaDSV.Get() : m_depthStencilView.Get();
    }
    ID3D11RenderTargetView* GetBackBufferRTV() const { return m_renderTargetView.Get(); }
    ID3D11DepthStencilView* GetNonMSAADSV() const { return m_depthStencilView.Get(); }
    ID3D11ShaderResourceView* GetDepthSRV() const { return m_depthSRV.Get(); }

    // Render stats — call after each DrawIndexed / Draw
    void TrackDrawCall(uint32_t indexCount);
    const RenderStats& GetStats() const { return m_stats; }

    // GPU info
    const GPUInfo& GetGPUInfo() const { return m_gpuInfo; }

private:
    bool CreateDeviceAndSwapChain(HWND hwnd);
    bool CreateRenderTargetView();
    bool CreateDepthStencilView();
    bool CreateMSAATargets();
    void SetViewport();
    bool CreateRasterizerStates();
    bool CreateDepthStencilStates();
    bool CreateBlendStates();
    bool CreateSamplerStates();
    void QueryGPUInfo();

    ComPtr<ID3D11Device>            m_device;
    ComPtr<ID3D11DeviceContext>     m_context;
    ComPtr<IDXGISwapChain>          m_swapChain;

    // Non-MSAA back buffer (swap chain)
    ComPtr<ID3D11Texture2D>         m_backBuffer;
    ComPtr<ID3D11RenderTargetView>  m_renderTargetView;
    ComPtr<ID3D11DepthStencilView>  m_depthStencilView;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;  // Depth as readable texture (for SSAO)
    ComPtr<ID3D11Texture2D>         m_depthStencilBuffer;

    // MSAA render targets (used when m_sampleCount > 1)
    ComPtr<ID3D11Texture2D>         m_msaaColorBuffer;
    ComPtr<ID3D11RenderTargetView>  m_msaaRTV;
    ComPtr<ID3D11Texture2D>         m_msaaDepthBuffer;
    ComPtr<ID3D11DepthStencilView>  m_msaaDSV;

    // Rasterizer states
    ComPtr<ID3D11RasterizerState>   m_rasterizerSolid;
    ComPtr<ID3D11RasterizerState>   m_rasterizerWireframe;
    ComPtr<ID3D11RasterizerState>   m_rasterizerNoCull;

    // Depth stencil states
    ComPtr<ID3D11DepthStencilState> m_depthEnabled;
    ComPtr<ID3D11DepthStencilState> m_depthDisabled;

    // Blend states
    ComPtr<ID3D11BlendState>        m_blendAlpha;
    ComPtr<ID3D11BlendState>        m_blendOpaque;

    // Sampler states (bound to PS slots 0/1/2)
    ComPtr<ID3D11SamplerState>      m_samplerPoint;   // s0
    ComPtr<ID3D11SamplerState>      m_samplerLinear;  // s1
    ComPtr<ID3D11SamplerState>      m_samplerAniso;   // s2

    int  m_width       = 0;
    int  m_height      = 0;
    bool m_vsync       = false;
    UINT m_sampleCount = 4;     // MSAA sample count (1 = off)

    RenderStats m_stats;
    GPUInfo     m_gpuInfo;
    bool m_skipMSAAResolve = false;
};

} // namespace WT
