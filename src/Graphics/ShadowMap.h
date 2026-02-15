#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class ShadowMap {
public:
    bool Init(ID3D11Device* device, UINT resolution = 2048);
    void Shutdown();

    // Begin shadow pass: sets DSV as render target, clears depth
    void BeginShadowPass(ID3D11DeviceContext* ctx);

    // End shadow pass: unbinds render target
    void EndShadowPass(ID3D11DeviceContext* ctx);

    // Bind shadow map as SRV for sampling in main pass
    void BindSRV(ID3D11DeviceContext* ctx, UINT slot) const;

    // Unbind SRV (must do before next shadow pass)
    void UnbindSRV(ID3D11DeviceContext* ctx, UINT slot) const;

    // Build light-space view-projection from directional light
    XMMATRIX CalcLightViewProjection(XMFLOAT3 lightDir, XMFLOAT3 sceneCenter,
                                      float sceneRadius) const;

    // Accessors
    UINT GetResolution() const { return m_resolution; }
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    ID3D11SamplerState* GetComparisonSampler() const { return m_comparisonSampler.Get(); }

private:
    ComPtr<ID3D11Texture2D>          m_depthTexture;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11SamplerState>       m_comparisonSampler;

    UINT m_resolution = 2048;
    D3D11_VIEWPORT m_viewport = {};
};

} // namespace WT
