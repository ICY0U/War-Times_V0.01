#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct SSAOSettings {
    bool  enabled        = false;
    float radius         = 0.3f;     // Sample hemisphere radius
    float bias           = 0.025f;   // Depth bias to prevent self-occlusion
    float intensity      = 0.5f;     // AO strength
    int   kernelSize     = 16;       // Number of samples (max 64)
    float noiseScale     = 4.0f;     // Tiling of noise texture
};

class SSAO {
public:
    bool Init(ID3D11Device* device, int width, int height,
              const std::wstring& shaderDir);
    void Shutdown();
    void OnResize(ID3D11Device* device, int width, int height);

    // Render SSAO from depth buffer + normals
    // Outputs to internal AO texture
    void Compute(ID3D11DeviceContext* ctx,
                 ID3D11ShaderResourceView* depthSRV,
                 const XMMATRIX& projection,
                 const XMMATRIX& view,
                 float nearZ, float farZ,
                 const SSAOSettings& settings);

    // Get the AO result texture (bind to pixel shader)
    ID3D11ShaderResourceView* GetAOTexture() const { return m_aoBlurSRV.Get(); }

    // Unbind resources after use
    void Unbind(ID3D11DeviceContext* ctx);

private:
    bool CreateTargets(ID3D11Device* device, int width, int height);
    bool CreateNoiseTexture(ID3D11Device* device);
    void GenerateKernel();
    void DrawFullscreenTriangle(ID3D11DeviceContext* ctx);

    int m_width = 0;
    int m_height = 0;

    // Sample kernel (random hemisphere directions)
    std::vector<XMFLOAT4> m_kernel;

    // AO output (full resolution)
    ComPtr<ID3D11Texture2D>          m_aoTexture;
    ComPtr<ID3D11RenderTargetView>   m_aoRTV;
    ComPtr<ID3D11ShaderResourceView> m_aoSRV;

    // Blurred AO
    ComPtr<ID3D11Texture2D>          m_aoBlurTexture;
    ComPtr<ID3D11RenderTargetView>   m_aoBlurRTV;
    ComPtr<ID3D11ShaderResourceView> m_aoBlurSRV;

    // Noise texture (4x4 random tangent-space rotations)
    ComPtr<ID3D11Texture2D>          m_noiseTexture;
    ComPtr<ID3D11ShaderResourceView> m_noiseSRV;

    // Shaders
    ComPtr<ID3D11VertexShader>  m_fullscreenVS;
    ComPtr<ID3D11PixelShader>   m_ssaoPS;
    ComPtr<ID3D11PixelShader>   m_blurPS;

    // Constant buffer
    ComPtr<ID3D11Buffer>        m_ssaoCB;
};

} // namespace WT
