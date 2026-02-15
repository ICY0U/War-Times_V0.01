#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>

namespace WT {

using Microsoft::WRL::ComPtr;

// Post-processing parameters — editable from editor
struct PostProcessSettings {
    // Bloom
    bool  bloomEnabled     = true;
    float bloomThreshold   = 0.8f;
    float bloomIntensity   = 0.5f;

    // Vignette
    bool  vignetteEnabled  = true;
    float vignetteIntensity = 0.4f;
    float vignetteSmoothness = 0.8f;

    // Color grading
    float brightness   = 0.0f;  // -1 to 1
    float contrast     = 1.0f;  // 0 to 2
    float saturation   = 1.0f;  // 0 to 2
    float gamma        = 1.0f;  // 0.5 to 2
    float tint[3]      = { 1.0f, 1.0f, 1.0f };

    // SSAO control (SSAO computed externally, but composite needs to know)
    bool ssaoEnabled   = false;

    // Art Style: Ink outlines
    bool  outlineEnabled        = false;
    float outlineThickness      = 1.0f;   // 0.5 - 3.0
    float outlineDepthThreshold = 0.1f;
    float outlineColor[3]       = { 0.05f, 0.03f, 0.02f };  // Near-black ink

    // Art Style: Paper grain
    float paperGrainIntensity   = 0.0f;   // 0 - 0.15

    // Art Style: Hatching
    float hatchingIntensity     = 0.0f;   // 0 - 1.0
    float hatchingScale         = 4.0f;   // pixels per hatch line
};

class PostProcess {
public:
    bool Init(ID3D11Device* device, int width, int height,
              const std::wstring& shaderDir);
    void Shutdown();
    void OnResize(ID3D11Device* device, int width, int height);

    // Call before rendering scene — redirects output to HDR buffer
    void BeginSceneCapture(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);

    // Call after scene is rendered — applies post-processing and outputs to target
    void Apply(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* outputRTV,
               const PostProcessSettings& settings,
               ID3D11ShaderResourceView* depthSRV = nullptr);

    // Get the scene HDR SRV (for SSAO or other effects to reference)
    ID3D11ShaderResourceView* GetSceneSRV() const { return m_sceneSRV.Get(); }

private:
    bool CreateTargets(ID3D11Device* device, int width, int height);
    bool CreateBloomTargets(ID3D11Device* device, int width, int height);
    void DrawFullscreenTriangle(ID3D11DeviceContext* ctx);

    int m_width = 0;
    int m_height = 0;

    // Scene HDR buffer (full resolution)
    ComPtr<ID3D11Texture2D>          m_sceneTexture;
    ComPtr<ID3D11RenderTargetView>   m_sceneRTV;
    ComPtr<ID3D11ShaderResourceView> m_sceneSRV;

    // Bloom: half-res ping-pong buffers
    ComPtr<ID3D11Texture2D>          m_bloomTexA;
    ComPtr<ID3D11RenderTargetView>   m_bloomRTV_A;
    ComPtr<ID3D11ShaderResourceView> m_bloomSRV_A;
    ComPtr<ID3D11Texture2D>          m_bloomTexB;
    ComPtr<ID3D11RenderTargetView>   m_bloomRTV_B;
    ComPtr<ID3D11ShaderResourceView> m_bloomSRV_B;

    // Shaders
    ComPtr<ID3D11VertexShader>  m_fullscreenVS;
    ComPtr<ID3D11PixelShader>   m_bloomExtractPS;
    ComPtr<ID3D11PixelShader>   m_bloomBlurPS;
    ComPtr<ID3D11PixelShader>   m_compositePS;

    // Post-process constant buffer
    ComPtr<ID3D11Buffer>        m_postCB;
};

} // namespace WT
