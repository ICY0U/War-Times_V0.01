#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace WT {

using Microsoft::WRL::ComPtr;

class Texture {
public:
    // Create a 1x1 solid color texture
    bool CreateFromColor(ID3D11Device* device, float r, float g, float b, float a = 1.0f);

    // Create from raw RGBA pixel data
    bool CreateFromData(ID3D11Device* device, const void* data, int width, int height,
                        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT bytesPerPixel = 4);

    // Load from a BMP image file
    bool LoadFromBMP(ID3D11Device* device, const std::wstring& filepath);

    // Load from a PNG image file (uses WIC)
    bool LoadFromPNG(ID3D11Device* device, const std::wstring& filepath);

    // Create a procedural dev grid texture (prototype placeholder)
    // baseColor: fill color, lineColor: grid line color, gridCells: number of grid cells per axis
    bool CreateGridTexture(ID3D11Device* device, int texSize,
                           float baseR, float baseG, float baseB,
                           float lineR, float lineG, float lineB,
                           int gridCells = 8, int lineWidth = 2);

    // Bind to pixel shader texture slot
    void Bind(ID3D11DeviceContext* context, UINT slot) const;
    void BindVS(ID3D11DeviceContext* context, UINT slot) const;

    // Accessors
    ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
    int  GetWidth()  const { return m_width; }
    int  GetHeight() const { return m_height; }
    bool IsValid()   const { return m_srv != nullptr; }

    void Release();

private:
    ComPtr<ID3D11Texture2D>          m_texture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    int m_width  = 0;
    int m_height = 0;
};

} // namespace WT
