#include "PostProcess.h"
#include "Util/Log.h"
#include <d3dcompiler.h>

namespace WT {

// CB layout for post-processing parameters — must match PostProcessPS.hlsl
struct CBPostProcess {
    float BloomThreshold;
    float BloomIntensity;
    float VignetteIntensity;
    float VignetteSmoothness;
    float Brightness;
    float Contrast;
    float Saturation;
    float Gamma;
    float Tint[3];
    float BlurDirection;  // 0 = horizontal, 1 = vertical
    float TexelSizeX;
    float TexelSizeY;
    int   BloomEnabled;
    int   VignetteEnabled;
    int   SSAOEnabled;
    int   OutlineEnabled;
    float OutlineThickness;
    float OutlineDepthThreshold;
    float OutlineNormalThreshold;
    float PaperGrainIntensity;
    float HatchingIntensity;
    float HatchingScale;
    float OutlineColor[3];
    float _postPad1;
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
        if (errors) LOG_ERROR("Shader compile error: %s", (char*)errors->GetBufferPointer());
        return nullptr;
    }
    return blob;
}

bool PostProcess::Init(ID3D11Device* device, int width, int height,
                       const std::wstring& shaderDir) {
    m_width = width;
    m_height = height;

    if (!CreateTargets(device, width, height)) return false;
    if (!CreateBloomTargets(device, width, height)) return false;

    // Compile post-process shaders
    auto vsBlob = CompileShaderBlob(shaderDir + L"PostProcessVS.hlsl", "VSMain", "vs_5_0");
    if (!vsBlob) return false;
    HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                             nullptr, m_fullscreenVS.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreateVS");

    auto extractBlob = CompileShaderBlob(shaderDir + L"PostProcessPS.hlsl", "BloomExtract", "ps_5_0");
    if (!extractBlob) return false;
    hr = device->CreatePixelShader(extractBlob->GetBufferPointer(), extractBlob->GetBufferSize(),
                                    nullptr, m_bloomExtractPS.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreatePS (bloom extract)");

    auto blurBlob = CompileShaderBlob(shaderDir + L"PostProcessPS.hlsl", "BloomBlur", "ps_5_0");
    if (!blurBlob) return false;
    hr = device->CreatePixelShader(blurBlob->GetBufferPointer(), blurBlob->GetBufferSize(),
                                    nullptr, m_bloomBlurPS.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreatePS (bloom blur)");

    auto compositeBlob = CompileShaderBlob(shaderDir + L"PostProcessPS.hlsl", "Composite", "ps_5_0");
    if (!compositeBlob) return false;
    hr = device->CreatePixelShader(compositeBlob->GetBufferPointer(), compositeBlob->GetBufferSize(),
                                    nullptr, m_compositePS.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreatePS (composite)");

    // Create constant buffer
    D3D11_BUFFER_DESC bd = {};
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth      = sizeof(CBPostProcess);
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&bd, nullptr, m_postCB.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreateCB");

    LOG_INFO("Post-processing initialized (%dx%d)", width, height);
    return true;
}

void PostProcess::Shutdown() {
    m_sceneTexture.Reset();
    m_sceneRTV.Reset();
    m_sceneSRV.Reset();
    m_bloomTexA.Reset();
    m_bloomRTV_A.Reset();
    m_bloomSRV_A.Reset();
    m_bloomTexB.Reset();
    m_bloomRTV_B.Reset();
    m_bloomSRV_B.Reset();
    m_fullscreenVS.Reset();
    m_bloomExtractPS.Reset();
    m_bloomBlurPS.Reset();
    m_compositePS.Reset();
    m_postCB.Reset();
}

void PostProcess::OnResize(ID3D11Device* device, int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_width = width;
    m_height = height;

    m_sceneTexture.Reset();
    m_sceneRTV.Reset();
    m_sceneSRV.Reset();
    m_bloomTexA.Reset();
    m_bloomRTV_A.Reset();
    m_bloomSRV_A.Reset();
    m_bloomTexB.Reset();
    m_bloomRTV_B.Reset();
    m_bloomSRV_B.Reset();

    CreateTargets(device, width, height);
    CreateBloomTargets(device, width, height);
}

bool PostProcess::CreateTargets(ID3D11Device* device, int width, int height) {
    // HDR scene buffer (R16G16B16A16_FLOAT for HDR range)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = width;
    texDesc.Height    = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, m_sceneTexture.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreateSceneTexture");

    hr = device->CreateRenderTargetView(m_sceneTexture.Get(), nullptr, m_sceneRTV.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreateSceneRTV");

    hr = device->CreateShaderResourceView(m_sceneTexture.Get(), nullptr, m_sceneSRV.GetAddressOf());
    HR_CHECK(hr, "PostProcess CreateSceneSRV");

    return true;
}

bool PostProcess::CreateBloomTargets(ID3D11Device* device, int width, int height) {
    int bw = width / 2;
    int bh = height / 2;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = bw;
    texDesc.Height    = bh;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr;

    hr = device->CreateTexture2D(&texDesc, nullptr, m_bloomTexA.GetAddressOf());
    HR_CHECK(hr, "PostProcess bloom A tex");
    hr = device->CreateRenderTargetView(m_bloomTexA.Get(), nullptr, m_bloomRTV_A.GetAddressOf());
    HR_CHECK(hr, "PostProcess bloom A RTV");
    hr = device->CreateShaderResourceView(m_bloomTexA.Get(), nullptr, m_bloomSRV_A.GetAddressOf());
    HR_CHECK(hr, "PostProcess bloom A SRV");

    hr = device->CreateTexture2D(&texDesc, nullptr, m_bloomTexB.GetAddressOf());
    HR_CHECK(hr, "PostProcess bloom B tex");
    hr = device->CreateRenderTargetView(m_bloomTexB.Get(), nullptr, m_bloomRTV_B.GetAddressOf());
    HR_CHECK(hr, "PostProcess bloom B RTV");
    hr = device->CreateShaderResourceView(m_bloomTexB.Get(), nullptr, m_bloomSRV_B.GetAddressOf());
    HR_CHECK(hr, "PostProcess bloom B SRV");

    return true;
}

void PostProcess::DrawFullscreenTriangle(ID3D11DeviceContext* ctx) {
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->Draw(3, 0);
}

void PostProcess::BeginSceneCapture(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv) {
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->ClearRenderTargetView(m_sceneRTV.Get(), clearColor);
    ctx->OMSetRenderTargets(1, m_sceneRTV.GetAddressOf(), dsv);
}

void PostProcess::Apply(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* outputRTV,
                         const PostProcessSettings& settings,
                         ID3D11ShaderResourceView* depthSRV) {
    // Update constant buffer
    CBPostProcess cbData = {};
    cbData.BloomThreshold    = settings.bloomThreshold;
    cbData.BloomIntensity    = settings.bloomIntensity;
    cbData.VignetteIntensity = settings.vignetteIntensity;
    cbData.VignetteSmoothness = settings.vignetteSmoothness;
    cbData.Brightness  = settings.brightness;
    cbData.Contrast    = settings.contrast;
    cbData.Saturation  = settings.saturation;
    cbData.Gamma       = settings.gamma;
    cbData.Tint[0]     = settings.tint[0];
    cbData.Tint[1]     = settings.tint[1];
    cbData.Tint[2]     = settings.tint[2];
    cbData.TexelSizeX  = 2.0f / static_cast<float>(m_width);   // Half-res texel
    cbData.TexelSizeY  = 2.0f / static_cast<float>(m_height);
    cbData.BloomEnabled = settings.bloomEnabled ? 1 : 0;
    cbData.VignetteEnabled = settings.vignetteEnabled ? 1 : 0;
    cbData.SSAOEnabled     = settings.ssaoEnabled ? 1 : 0;
    cbData.OutlineEnabled    = settings.outlineEnabled ? 1 : 0;
    cbData.OutlineThickness  = settings.outlineThickness;
    cbData.OutlineDepthThreshold = settings.outlineDepthThreshold;
    cbData.OutlineNormalThreshold = 0.0f;
    cbData.PaperGrainIntensity = settings.paperGrainIntensity;
    cbData.HatchingIntensity   = settings.hatchingIntensity;
    cbData.HatchingScale       = settings.hatchingScale;
    cbData.OutlineColor[0] = settings.outlineColor[0];
    cbData.OutlineColor[1] = settings.outlineColor[1];
    cbData.OutlineColor[2] = settings.outlineColor[2];

    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(m_postCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cbData, sizeof(cbData));
    ctx->Unmap(m_postCB.Get(), 0);

    ctx->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(5, 1, m_postCB.GetAddressOf());

    // ---- Bloom Extract: scene -> bloomA ----
    if (settings.bloomEnabled) {
        D3D11_VIEWPORT bloomVP = {};
        bloomVP.Width  = static_cast<float>(m_width / 2);
        bloomVP.Height = static_cast<float>(m_height / 2);
        bloomVP.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &bloomVP);

        ctx->OMSetRenderTargets(1, m_bloomRTV_A.GetAddressOf(), nullptr);
        ID3D11ShaderResourceView* sceneSRV = m_sceneSRV.Get();
        ctx->PSSetShaderResources(0, 1, &sceneSRV);
        ctx->PSSetShader(m_bloomExtractPS.Get(), nullptr, 0);
        DrawFullscreenTriangle(ctx);

        // ---- Bloom Blur: horizontal (bloomA -> bloomB) ----
        cbData.BlurDirection = 0.0f;
        ctx->Map(m_postCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &cbData, sizeof(cbData));
        ctx->Unmap(m_postCB.Get(), 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);
        ctx->OMSetRenderTargets(1, m_bloomRTV_B.GetAddressOf(), nullptr);
        ID3D11ShaderResourceView* bloomA = m_bloomSRV_A.Get();
        ctx->PSSetShaderResources(0, 1, &bloomA);
        ctx->PSSetShader(m_bloomBlurPS.Get(), nullptr, 0);
        DrawFullscreenTriangle(ctx);

        // ---- Bloom Blur: vertical (bloomB -> bloomA) ----
        cbData.BlurDirection = 1.0f;
        ctx->Map(m_postCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &cbData, sizeof(cbData));
        ctx->Unmap(m_postCB.Get(), 0);

        ctx->PSSetShaderResources(0, 1, &nullSRV);
        ctx->OMSetRenderTargets(1, m_bloomRTV_A.GetAddressOf(), nullptr);
        ID3D11ShaderResourceView* bloomB = m_bloomSRV_B.Get();
        ctx->PSSetShaderResources(0, 1, &bloomB);
        DrawFullscreenTriangle(ctx);
    }

    // ---- Composite: scene + bloom -> output ----
    D3D11_VIEWPORT fullVP = {};
    fullVP.Width  = static_cast<float>(m_width);
    fullVP.Height = static_cast<float>(m_height);
    fullVP.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &fullVP);

    // Unbind SRVs first
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(0, 2, nullSRVs);

    ctx->OMSetRenderTargets(1, &outputRTV, nullptr);
    ID3D11ShaderResourceView* compositeSRVs[2] = { m_sceneSRV.Get(), m_bloomSRV_A.Get() };
    ctx->PSSetShaderResources(0, 2, compositeSRVs);

    // Bind depth buffer at t2 for outline edge detection
    if (depthSRV) {
        ctx->PSSetShaderResources(2, 1, &depthSRV);
    }

    ctx->PSSetShader(m_compositePS.Get(), nullptr, 0);
    DrawFullscreenTriangle(ctx);

    // Unbind SRVs (including depth at t2)
    ID3D11ShaderResourceView* nullSRVs3[3] = { nullptr, nullptr, nullptr };
    ctx->PSSetShaderResources(0, 3, nullSRVs3);
}

} // namespace WT
