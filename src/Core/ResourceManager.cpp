#include "ResourceManager.h"
#include "Util/Log.h"
#include <filesystem>

namespace WT {

void ResourceManager::Init(ID3D11Device* device, const std::wstring& shaderDir) {
    m_device    = device;
    m_shaderDir = shaderDir;
    LOG_INFO("ResourceManager initialized");
}

void ResourceManager::Shutdown() {
    for (auto& [name, res] : m_meshes)    res.data.Release();
    for (auto& [name, res] : m_textures)  res.data.Release();
    m_meshes.clear();
    m_shaders.clear();
    m_textures.clear();
    LOG_INFO("ResourceManager shutdown (%d meshes, %d shaders, %d textures released)",
             0, 0, 0);
}

// ==================== Mesh ====================

void ResourceManager::RegisterMesh(const std::string& name, Mesh&& mesh) {
    auto it = m_meshes.find(name);
    if (it != m_meshes.end()) {
        it->second.refCount++;
        LOG_WARN("Mesh '%s' already registered (refcount: %d)", name.c_str(), it->second.refCount);
        return;
    }
    Resource<Mesh> res;
    res.data     = std::move(mesh);
    res.name     = name;
    res.refCount = 1;
    m_meshes[name] = std::move(res);
    LOG_INFO("Registered mesh: %s", name.c_str());
}

Mesh* ResourceManager::GetMesh(const std::string& name) {
    auto it = m_meshes.find(name);
    if (it == m_meshes.end()) return nullptr;
    return &it->second.data;
}

void ResourceManager::ReleaseMesh(const std::string& name) {
    auto it = m_meshes.find(name);
    if (it == m_meshes.end()) return;
    it->second.refCount--;
    if (it->second.refCount <= 0) {
        it->second.data.Release();
        m_meshes.erase(it);
        LOG_INFO("Released mesh: %s", name.c_str());
    }
}

// ==================== OBJ Loading ====================

bool ResourceManager::LoadOBJ(const std::string& name, const std::wstring& filepath,
                                const DirectX::XMFLOAT4& defaultColor) {
    // Already loaded?
    if (m_meshes.find(name) != m_meshes.end()) {
        LOG_INFO("Model '%s' already loaded", name.c_str());
        return true;
    }

    auto result = OBJLoader::Load(filepath, defaultColor);
    if (!result.success || result.vertices.empty()) {
        LOG_ERROR("Failed to load OBJ model '%s'", name.c_str());
        return false;
    }

    Mesh mesh;
    if (!mesh.Create(m_device, result.vertices, result.indices)) {
        LOG_ERROR("Failed to create mesh for model '%s'", name.c_str());
        return false;
    }

    RegisterMesh(name, std::move(mesh));
    LOG_INFO("Loaded OBJ model: %s (%d tris)", name.c_str(), result.triangleCount);
    return true;
}

int ResourceManager::LoadOBJDirectory(const std::wstring& dirPath) {
    namespace fs = std::filesystem;

    int count = 0;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        LOG_WARN("Models directory not found, skipping OBJ loading");
        return 0;
    }

    // Use recursive iterator to also load subdirectories (e.g. models/Guns/)
    for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().wstring();
        // Case-insensitive extension check
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
        if (ext != L".obj") continue;

        // Build a name with subdirectory prefix: "Guns/AssaultRiffle_A" or just "barrel"
        fs::path relPath = fs::relative(entry.path(), dirPath);
        std::string name = relPath.replace_extension("").string();
        // Normalize path separators to forward slash
        std::replace(name.begin(), name.end(), '\\', '/');
        std::wstring fullPath = entry.path().wstring();

        if (LoadOBJ(name, fullPath)) {
            count++;
        }
    }

    LOG_INFO("Loaded %d OBJ models from directory (recursive)", count);
    return count;
}

std::vector<std::string> ResourceManager::GetModelNames() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : m_meshes) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// ==================== Shader ====================

bool ResourceManager::LoadShader(const std::string& name,
                                  const std::wstring& vsPath, const char* vsEntry,
                                  const std::wstring& psPath, const char* psEntry,
                                  const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount) {
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        it->second.refCount++;
        return true;
    }

    Resource<Shader> res;
    res.name = name;
    if (!res.data.LoadVS(m_device, vsPath, vsEntry, layout, layoutCount)) {
        LOG_ERROR("Failed to load VS for shader '%s'", name.c_str());
        return false;
    }
    if (!res.data.LoadPS(m_device, psPath, psEntry)) {
        LOG_ERROR("Failed to load PS for shader '%s'", name.c_str());
        return false;
    }
    m_shaders[name] = std::move(res);
    LOG_INFO("Loaded shader: %s", name.c_str());
    return true;
}

Shader* ResourceManager::GetShader(const std::string& name) {
    auto it = m_shaders.find(name);
    if (it == m_shaders.end()) return nullptr;
    return &it->second.data;
}

void ResourceManager::ReleaseShader(const std::string& name) {
    auto it = m_shaders.find(name);
    if (it == m_shaders.end()) return;
    it->second.refCount--;
    if (it->second.refCount <= 0) {
        m_shaders.erase(it);
        LOG_INFO("Released shader: %s", name.c_str());
    }
}

// ==================== Texture ====================

bool ResourceManager::CreateColorTexture(const std::string& name, float r, float g, float b, float a) {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        it->second.refCount++;
        return true;
    }

    Resource<Texture> res;
    res.name = name;
    if (!res.data.CreateFromColor(m_device, r, g, b, a)) {
        LOG_ERROR("Failed to create color texture '%s'", name.c_str());
        return false;
    }
    m_textures[name] = std::move(res);
    LOG_INFO("Created texture: %s", name.c_str());
    return true;
}

bool ResourceManager::LoadTextureBMP(const std::string& name, const std::wstring& filepath) {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        LOG_INFO("Texture '%s' already loaded", name.c_str());
        return true;
    }

    Resource<Texture> res;
    res.name = name;
    if (!res.data.LoadFromBMP(m_device, filepath)) {
        LOG_ERROR("Failed to load BMP texture '%s'", name.c_str());
        return false;
    }
    m_textures[name] = std::move(res);
    LOG_INFO("Loaded BMP texture: %s", name.c_str());
    return true;
}

int ResourceManager::LoadTextureDirectory(const std::wstring& dirPath) {
    namespace fs = std::filesystem;

    int count = 0;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        LOG_WARN("Texture directory not found, skipping");
        return 0;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
        if (ext != L".bmp") continue;

        // Build name matching model naming: "Guns/AssaultRiffle_A"
        fs::path relPath = fs::relative(entry.path(), dirPath);
        std::string name = relPath.replace_extension("").string();
        std::replace(name.begin(), name.end(), '\\', '/');

        if (LoadTextureBMP(name, entry.path().wstring())) {
            count++;
        }
    }

    LOG_INFO("Loaded %d BMP textures from directory (recursive)", count);
    return count;
}

Texture* ResourceManager::GetTexture(const std::string& name) {
    auto it = m_textures.find(name);
    if (it == m_textures.end()) return nullptr;
    return &it->second.data;
}

void ResourceManager::ReleaseTexture(const std::string& name) {
    auto it = m_textures.find(name);
    if (it == m_textures.end()) return;
    it->second.refCount--;
    if (it->second.refCount <= 0) {
        it->second.data.Release();
        m_textures.erase(it);
        LOG_INFO("Released texture: %s", name.c_str());
    }
}

// ==================== Hot Reload ====================

void ResourceManager::CheckHotReload() {
    for (auto& [name, res] : m_shaders) {
        if (res.data.HasFileChanged()) {
            if (res.data.Reload(m_device)) {
                LOG_INFO("Hot-reloaded shader: %s", name.c_str());
            }
        }
    }
}

} // namespace WT
