#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include "Graphics/Mesh.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture.h"
#include "Graphics/OBJLoader.h"

namespace WT {

// ---- Typed resource handle with reference counting ----
template<typename T>
struct Resource {
    T           data;
    std::string name;
    int         refCount = 1;
};

// ---- Resource Manager — centralized loading, caching, and hot-reload ----
class ResourceManager {
public:
    static ResourceManager& Get() { static ResourceManager mgr; return mgr; }

    void Init(ID3D11Device* device, const std::wstring& shaderDir);
    void Shutdown();

    // ---- Mesh Resources ----
    // Store a pre-built mesh under a name
    void RegisterMesh(const std::string& name, Mesh&& mesh);
    Mesh* GetMesh(const std::string& name);
    void  ReleaseMesh(const std::string& name);

    // Load an OBJ model file, create a Mesh, and register it
    bool LoadOBJ(const std::string& name, const std::wstring& filepath,
                 const DirectX::XMFLOAT4& defaultColor = { 0.6f, 0.6f, 0.6f, 1.0f });

    // Scan a directory for .obj files and load them all
    int  LoadOBJDirectory(const std::wstring& dirPath);

    // Get list of loaded model names (for editor dropdown)
    std::vector<std::string> GetModelNames() const;

    // ---- Shader Resources ----
    // Load VS+PS from file, cache by name
    bool LoadShader(const std::string& name,
                    const std::wstring& vsPath, const char* vsEntry,
                    const std::wstring& psPath, const char* psEntry,
                    const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount);
    Shader* GetShader(const std::string& name);
    void    ReleaseShader(const std::string& name);

    // ---- Texture Resources ----
    // Create a solid-color texture, cache by name
    bool CreateColorTexture(const std::string& name, float r, float g, float b, float a = 1.0f);
    // Load a BMP texture from file, cache by name
    bool LoadTextureBMP(const std::string& name, const std::wstring& filepath);
    // Load a PNG texture from file, cache by name
    bool LoadTexturePNG(const std::string& name, const std::wstring& filepath);
    // Scan a directory for .bmp/.png files and load them
    int  LoadTextureDirectory(const std::wstring& dirPath);
    Texture* GetTexture(const std::string& name);
    void     ReleaseTexture(const std::string& name);

    // ---- Hot-Reload ----
    // Check all shaders for file changes, reload modified ones
    void CheckHotReload();

    // ---- Stats ----
    int GetMeshCount() const    { return static_cast<int>(m_meshes.size()); }
    int GetShaderCount() const  { return static_cast<int>(m_shaders.size()); }
    int GetTextureCount() const { return static_cast<int>(m_textures.size()); }

    // Iterate resources (for editor display)
    const auto& GetMeshes() const    { return m_meshes; }
    const auto& GetShaders() const   { return m_shaders; }
    const auto& GetTextures() const  { return m_textures; }

    // Sorted list of texture names (for editor combo)
    std::vector<std::string> GetTextureNames() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : m_textures) {
            if (name[0] == '_') continue;  // skip internal textures
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

private:
    ResourceManager() = default;

    ID3D11Device* m_device = nullptr;
    std::wstring  m_shaderDir;

    std::unordered_map<std::string, Resource<Mesh>>    m_meshes;
    std::unordered_map<std::string, Resource<Shader>>  m_shaders;
    std::unordered_map<std::string, Resource<Texture>> m_textures;
};

} // namespace WT
