#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <Windows.h>
#include <string>
#include <vector>

namespace WT {

using Microsoft::WRL::ComPtr;

class Shader {
public:
    // Load and compile from file
    bool LoadVS(ID3D11Device* device, const std::wstring& path, const char* entryPoint,
                const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount);
    bool LoadPS(ID3D11Device* device, const std::wstring& path, const char* entryPoint);

    // Bind both shaders + input layout to the pipeline
    void Bind(ID3D11DeviceContext* context) const;

    bool IsValid() const { return m_vertexShader && m_pixelShader; }

    ID3D11VertexShader* GetVS() const { return m_vertexShader.Get(); }
    ID3D11PixelShader*  GetPS() const { return m_pixelShader.Get(); }
    ID3D11InputLayout*  GetInputLayout() const { return m_inputLayout.Get(); }

    // --- Hot-Reload ---
    bool HasFileChanged() const;
    bool Reload(ID3D11Device* device);

private:
    ComPtr<ID3DBlob> CompileShader(const std::wstring& path, const char* entryPoint, const char* target);
    FILETIME GetFileWriteTime(const std::wstring& path) const;
    void StoreLayoutDesc(const D3D11_INPUT_ELEMENT_DESC* layout, UINT count);

    ComPtr<ID3D11VertexShader>  m_vertexShader;
    ComPtr<ID3D11PixelShader>   m_pixelShader;
    ComPtr<ID3D11InputLayout>   m_inputLayout;

    // Hot-reload state
    std::wstring m_vsPath;
    std::wstring m_psPath;
    std::string  m_vsEntry;
    std::string  m_psEntry;
    std::vector<D3D11_INPUT_ELEMENT_DESC> m_layoutDesc;
    std::vector<std::string> m_semanticNames; // Keeps SemanticName strings alive
    FILETIME m_vsLastWrite = {};
    FILETIME m_psLastWrite = {};
};

} // namespace WT
