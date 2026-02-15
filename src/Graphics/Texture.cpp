#include "Texture.h"
#include "Util/Log.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

namespace WT {

bool Texture::CreateFromColor(ID3D11Device* device, float r, float g, float b, float a) {
    uint8_t pixel[4] = {
        static_cast<uint8_t>(r * 255.0f),
        static_cast<uint8_t>(g * 255.0f),
        static_cast<uint8_t>(b * 255.0f),
        static_cast<uint8_t>(a * 255.0f)
    };
    return CreateFromData(device, pixel, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
}

bool Texture::CreateFromData(ID3D11Device* device, const void* data, int width, int height,
                             DXGI_FORMAT format, UINT bytesPerPixel) {
    Release();

    m_width  = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width     = static_cast<UINT>(width);
    desc.Height    = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = format;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem     = data;
    initData.SysMemPitch = width * bytesPerPixel;

    HRESULT hr = device->CreateTexture2D(&desc, &initData, m_texture.GetAddressOf());
    HR_CHECK(hr, "CreateTexture2D");

    hr = device->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    HR_CHECK(hr, "CreateShaderResourceView");

    return true;
}

bool Texture::LoadFromBMP(ID3D11Device* device, const std::wstring& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Texture: Failed to open BMP file");
        return false;
    }

    // Read file header (14 bytes)
    uint8_t fileHeader[14];
    file.read(reinterpret_cast<char*>(fileHeader), 14);
    if (fileHeader[0] != 'B' || fileHeader[1] != 'M') {
        LOG_ERROR("Texture: Not a valid BMP file");
        return false;
    }

    uint32_t pixelOffset = *reinterpret_cast<uint32_t*>(&fileHeader[10]);

    // Read info header (at least 40 bytes)
    uint8_t infoHeader[40];
    file.read(reinterpret_cast<char*>(infoHeader), 40);

    int32_t width  = *reinterpret_cast<int32_t*>(&infoHeader[4]);
    int32_t height = *reinterpret_cast<int32_t*>(&infoHeader[8]);
    uint16_t bitsPerPixel = *reinterpret_cast<uint16_t*>(&infoHeader[14]);

    bool bottomUp = (height > 0);
    if (height < 0) height = -height;

    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        LOG_ERROR("Texture: Unsupported BMP bit depth: %d (need 24 or 32)", bitsPerPixel);
        return false;
    }

    // Seek to pixel data
    file.seekg(pixelOffset, std::ios::beg);

    int bytesPerPixel = bitsPerPixel / 8;
    int rowSize = width * bytesPerPixel;
    int rowPadding = (4 - (rowSize % 4)) % 4;
    int paddedRowSize = rowSize + rowPadding;

    // Read raw pixel data
    std::vector<uint8_t> rawData(paddedRowSize * height);
    file.read(reinterpret_cast<char*>(rawData.data()), rawData.size());

    // Convert to RGBA (top-to-bottom)
    std::vector<uint8_t> rgba(width * height * 4);
    for (int y = 0; y < height; y++) {
        int srcRow = bottomUp ? (height - 1 - y) : y;
        for (int x = 0; x < width; x++) {
            int srcIdx = srcRow * paddedRowSize + x * bytesPerPixel;
            int dstIdx = (y * width + x) * 4;

            // BMP stores BGR(A)
            rgba[dstIdx + 0] = rawData[srcIdx + 2]; // R
            rgba[dstIdx + 1] = rawData[srcIdx + 1]; // G
            rgba[dstIdx + 2] = rawData[srcIdx + 0]; // B
            rgba[dstIdx + 3] = (bytesPerPixel == 4) ? rawData[srcIdx + 3] : 255; // A
        }
    }

    bool ok = CreateFromData(device, rgba.data(), width, height);
    if (ok) {
        LOG_INFO("Texture: Loaded BMP %dx%d (%d bpp)", width, height, bitsPerPixel);
    }
    return ok;
}

bool Texture::LoadFromPNG(ID3D11Device* device, const std::wstring& filepath) {
    // Use Windows Imaging Component (WIC) to decode PNG
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr; // Don't care if already initialized

    ComPtr<IWICImagingFactory> wicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wicFactory.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR("Texture: Failed to create WIC factory (0x%08X)", hr);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(filepath.c_str(), nullptr,
                                                GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                decoder.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Texture: Failed to open PNG file (0x%08X)", hr);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Texture: Failed to get PNG frame (0x%08X)", hr);
        return false;
    }

    // Convert to RGBA 32bpp
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        LOG_ERROR("Texture: Failed to convert PNG to RGBA (0x%08X)", hr);
        return false;
    }

    UINT width, height;
    converter->GetSize(&width, &height);

    UINT stride = width * 4;
    UINT bufferSize = stride * height;
    std::vector<uint8_t> rgba(bufferSize);
    hr = converter->CopyPixels(nullptr, stride, bufferSize, rgba.data());
    if (FAILED(hr)) {
        LOG_ERROR("Texture: Failed to copy PNG pixels (0x%08X)", hr);
        return false;
    }

    bool ok = CreateFromData(device, rgba.data(), static_cast<int>(width), static_cast<int>(height));
    if (ok) {
        LOG_INFO("Texture: Loaded PNG %ux%u", width, height);
    }
    return ok;
}

void Texture::Bind(ID3D11DeviceContext* context, UINT slot) const {
    context->PSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

void Texture::BindVS(ID3D11DeviceContext* context, UINT slot) const {
    context->VSSetShaderResources(slot, 1, m_srv.GetAddressOf());
}

void Texture::Release() {
    m_srv.Reset();
    m_texture.Reset();
    m_width  = 0;
    m_height = 0;
}

} // namespace WT
