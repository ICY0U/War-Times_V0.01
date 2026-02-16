#pragma once
// ============================================================
// FSRUpscaler — AMD FSR 1.0-style spatial upscaling for D3D11
// EASU (Edge-Adaptive Spatial Upsampling) + RCAS (Robust Contrast-Adaptive Sharpening)
// ============================================================

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace WT {

using Microsoft::WRL::ComPtr;

// Quality presets (render scale relative to output resolution)
enum class FSRQuality : int {
    UltraQuality = 0,  // 77% render scale
    Quality      = 1,  // 67%
    Balanced     = 2,  // 58%
    Performance  = 3,  // 50%
    Count
};

inline const char* FSRQualityName(FSRQuality q) {
    switch (q) {
        case FSRQuality::UltraQuality: return "Ultra Quality (77%)";
        case FSRQuality::Quality:      return "Quality (67%)";
        case FSRQuality::Balanced:     return "Balanced (58%)";
        case FSRQuality::Performance:  return "Performance (50%)";
        default: return "Unknown";
    }
}

inline float FSRQualityScale(FSRQuality q) {
    switch (q) {
        case FSRQuality::UltraQuality: return 0.77f;
        case FSRQuality::Quality:      return 0.67f;
        case FSRQuality::Balanced:     return 0.58f;
        case FSRQuality::Performance:  return 0.50f;
        default: return 1.0f;
    }
}

// FSR constant buffer — must match FSRPS.hlsl
struct CBFSRParams {
    float InputSizeX;     // Render resolution width
    float InputSizeY;     // Render resolution height
    float OutputSizeX;    // Display resolution width
    float OutputSizeY;    // Display resolution height
    float RcasSharpness;  // RCAS sharpness (0 = max, 1 = no sharpen)
    int   PassMode;       // 0 = EASU, 1 = RCAS
    float _pad[2];
};

class FSRUpscaler {
public:
    bool Init(ID3D11Device* device, int outputWidth, int outputHeight,
              const std::wstring& shaderDir);
    void Shutdown();
    void OnResize(ID3D11Device* device, int outputWidth, int outputHeight);

    // Get render resolution for current quality preset
    void GetRenderResolution(int outputWidth, int outputHeight, FSRQuality quality,
                             int& outRenderW, int& outRenderH) const;

    // Create/recreate the intermediate render target at the given render resolution.
    // Call when quality preset or output resolution changes.
    void UpdateRenderTarget(ID3D11Device* device, int renderWidth, int renderHeight);

    // Get the intermediate render target RTV for post-process to write into
    ID3D11RenderTargetView* GetRenderRTV() const { return m_renderRTV.Get(); }
    ID3D11ShaderResourceView* GetRenderSRV() const { return m_renderSRV.Get(); }

    // Apply FSR upscaling: render-res SRV → output-res back buffer RTV
    // EASU upscales, then RCAS sharpens.
    void Apply(ID3D11DeviceContext* ctx,
               ID3D11RenderTargetView* outputRTV,
               int outputWidth, int outputHeight,
               float sharpness);

    int GetRenderWidth()  const { return m_renderWidth; }
    int GetRenderHeight() const { return m_renderHeight; }

private:
    void DrawFullscreenTriangle(ID3D11DeviceContext* ctx);

    int m_renderWidth  = 0;   // Input render resolution
    int m_renderHeight = 0;
    int m_outputWidth  = 0;   // Display resolution
    int m_outputHeight = 0;

    // Intermediate render target (render resolution, LDR)
    ComPtr<ID3D11Texture2D>          m_renderTexture;
    ComPtr<ID3D11RenderTargetView>   m_renderRTV;
    ComPtr<ID3D11ShaderResourceView> m_renderSRV;

    // Intermediate upscaled target (output resolution, for EASU → RCAS pipeline)
    ComPtr<ID3D11Texture2D>          m_upscaledTexture;
    ComPtr<ID3D11RenderTargetView>   m_upscaledRTV;
    ComPtr<ID3D11ShaderResourceView> m_upscaledSRV;

    // Shaders
    ComPtr<ID3D11VertexShader>  m_fullscreenVS;
    ComPtr<ID3D11PixelShader>   m_easuPS;
    ComPtr<ID3D11PixelShader>   m_rcasPS;

    // Constant buffer
    ComPtr<ID3D11Buffer>        m_fsrCB;

    // Linear sampler for EASU
    ComPtr<ID3D11SamplerState>  m_linearSampler;
};

} // namespace WT
