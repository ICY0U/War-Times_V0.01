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

void ResourceManager::RegisterMesh(const std::string& name, Mesh&& mesh,
                                    const std::filesystem::path& srcPath) {
    namespace fs = std::filesystem;
    auto it = m_meshes.find(name);
    if (it != m_meshes.end()) {
        // Replace existing mesh data (hot-reload)
        it->second.data.Release();
        it->second.data = std::move(mesh);
        if (!srcPath.empty()) {
            it->second.filePath  = srcPath;
            it->second.lastWrite = fs::last_write_time(srcPath);
        }
        LOG_INFO("Replaced mesh: %s", name.c_str());
        return;
    }
    Resource<Mesh> res;
    res.data     = std::move(mesh);
    res.name     = name;
    res.refCount = 1;
    if (!srcPath.empty()) {
        res.filePath  = srcPath;
        res.lastWrite = fs::last_write_time(srcPath);
    }
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

std::vector<std::string> ResourceManager::GetModelNames() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : m_meshes) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

int ResourceManager::LoadMeshDirectory(const std::wstring& dirPath) {
    m_modelsDir = dirPath;  // store for hot-reload rescan
    return MeshLoader::LoadDirectory(m_device, dirPath);
}

int ResourceManager::ReloadMeshDirectory() {
    if (m_modelsDir.empty()) return 0;
    namespace fs = std::filesystem;

    if (!fs::exists(m_modelsDir) || !fs::is_directory(m_modelsDir))
        return 0;

    int loaded = 0, updated = 0, removed = 0;

    // Build set of files currently on disk
    std::unordered_map<std::string, fs::path> diskFiles;
    for (auto& entry : fs::recursive_directory_iterator(m_modelsDir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().wstring();
        if (ext != L".mesh" && ext != L".MESH") continue;
        fs::path relPath = fs::relative(entry.path(), m_modelsDir);
        std::string meshName = relPath.replace_extension().generic_string();
        diskFiles[meshName] = entry.path();
    }

    // Check for new or updated files
    for (auto& [name, path] : diskFiles) {
        auto it = m_meshes.find(name);
        if (it == m_meshes.end()) {
            // New file
            Mesh mesh;
            if (MeshLoader::LoadMesh(m_device, path.wstring(), mesh)) {
                RegisterMesh(name, std::move(mesh), path);
                loaded++;
            }
        } else {
            // Check if file was modified
            auto diskTime = fs::last_write_time(path);
            if (diskTime != it->second.lastWrite) {
                Mesh mesh;
                if (MeshLoader::LoadMesh(m_device, path.wstring(), mesh)) {
                    RegisterMesh(name, std::move(mesh), path);
                    updated++;
                }
            }
        }
    }

    // Check for deleted files
    std::vector<std::string> toRemove;
    for (auto& [name, res] : m_meshes) {
        if (!res.filePath.empty() && diskFiles.find(name) == diskFiles.end()) {
            toRemove.push_back(name);
        }
    }
    for (auto& name : toRemove) {
        m_meshes[name].data.Release();
        m_meshes.erase(name);
        removed++;
    }

    if (loaded || updated || removed) {
        LOG_INFO("Model hot-reload: %d new, %d updated, %d removed", loaded, updated, removed);
    }
    return loaded + updated;
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

bool ResourceManager::LoadTexturePNG(const std::string& name, const std::wstring& filepath) {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        LOG_INFO("Texture '%s' already loaded", name.c_str());
        return true;
    }

    Resource<Texture> res;
    res.name = name;
    if (!res.data.LoadFromPNG(m_device, filepath)) {
        LOG_ERROR("Failed to load PNG texture '%s'", name.c_str());
        return false;
    }
    m_textures[name] = std::move(res);
    LOG_INFO("Loaded PNG texture: %s", name.c_str());
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

        bool isBmp = (ext == L".bmp");
        bool isPng = (ext == L".png");
        if (!isBmp && !isPng) continue;

        // Build name matching model naming: "Guns/AssaultRiffle_A"
        fs::path relPath = fs::relative(entry.path(), dirPath);
        std::string name = relPath.replace_extension("").string();
        std::replace(name.begin(), name.end(), '\\', '/');

        bool ok = false;
        if (isBmp) ok = LoadTextureBMP(name, entry.path().wstring());
        else       ok = LoadTexturePNG(name, entry.path().wstring());
        if (ok) count++;
    }

    LOG_INFO("Loaded %d textures from directory (recursive)", count);
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
