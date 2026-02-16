#include "ResourceManager.h"
#include "Util/Log.h"
#include <filesystem>
#include <algorithm>
#include <cstdint>

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

int ResourceManager::CreateDevTextures() {
    int count = 0;
    // Dev grid textures: 256x256 with 8-cell grid, subtle checkerboard

    // Walls — warm gray / tan with dark grid lines
    {
        Resource<Texture> res;
        res.name = "Walls/texture";
        if (res.data.CreateGridTexture(m_device, 256,
                0.60f, 0.55f, 0.48f,   // base: warm tan
                0.30f, 0.28f, 0.25f,   // lines: dark brown-gray
                8, 2)) {
            m_textures["Walls/texture"] = std::move(res);
            count++;
            LOG_INFO("Created dev texture: Walls/texture");
        }
    }

    // Floors — gray-green with dark grid lines
    {
        Resource<Texture> res;
        res.name = "Floors/texture";
        if (res.data.CreateGridTexture(m_device, 256,
                0.45f, 0.50f, 0.42f,   // base: muted olive-gray
                0.22f, 0.25f, 0.20f,   // lines: dark green-gray
                8, 2)) {
            m_textures["Floors/texture"] = std::move(res);
            count++;
            LOG_INFO("Created dev texture: Floors/texture");
        }
    }

    // Props — steel blue-gray with dark grid lines
    {
        Resource<Texture> res;
        res.name = "Props/texture";
        if (res.data.CreateGridTexture(m_device, 256,
                0.48f, 0.52f, 0.58f,   // base: steel blue-gray
                0.24f, 0.26f, 0.30f,   // lines: dark blue-gray
                8, 2)) {
            m_textures["Props/texture"] = std::move(res);
            count++;
            LOG_INFO("Created dev texture: Props/texture");
        }
    }

    // Environment — neutral mid-gray with dark lines
    {
        Resource<Texture> res;
        res.name = "Environment/texture";
        if (res.data.CreateGridTexture(m_device, 256,
                0.50f, 0.50f, 0.50f,   // base: neutral gray
                0.25f, 0.25f, 0.25f,   // lines: dark gray
                8, 2)) {
            m_textures["Environment/texture"] = std::move(res);
            count++;
            LOG_INFO("Created dev texture: Environment/texture");
        }
    }

    LOG_INFO("Created %d dev grid textures", count);

    // ============================================================
    // Detailed procedural textures for world materials
    // ============================================================
    auto hashNoise = [](int x, int y, int seed) -> float {
        int n = x * 374761393 + y * 668265263 + seed * 1274126177;
        n = (n << 13) ^ n;
        return 1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
    };
    auto clampF = [](float v) -> uint8_t { return static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, v)) * 255.0f); };

    // ---- Brick Wall Texture (256x256) ----
    {
        const int S = 256;
        std::vector<uint8_t> px(S * S * 4);
        int brickH = 16, brickW = 32, mortarW = 2;
        for (int y = 0; y < S; y++) {
            for (int x = 0; x < S; x++) {
                int row = y / brickH;
                int xOff = (row % 2 == 0) ? 0 : brickW / 2;
                int lx = (x + xOff) % brickW;
                int ly = y % brickH;
                bool mortar = (lx < mortarW || ly < mortarW);
                float n = hashNoise(x, y, 42) * 0.06f;
                float r, g, b;
                if (mortar) {
                    r = 0.50f + n; g = 0.48f + n; b = 0.44f + n;
                } else {
                    float rowVar = hashNoise(row, (x + xOff) / brickW, 7) * 0.08f;
                    r = 0.55f + rowVar + n; g = 0.32f + rowVar * 0.5f + n; b = 0.25f + n;
                }
                int idx = (y * S + x) * 4;
                px[idx] = clampF(r); px[idx+1] = clampF(g); px[idx+2] = clampF(b); px[idx+3] = 255;
            }
        }
        Resource<Texture> res; res.name = "Buildings/brick";
        if (res.data.CreateFromData(m_device, px.data(), S, S)) {
            m_textures["Buildings/brick"] = std::move(res); count++;
        }
    }

    // ---- Roof Shingle Texture (256x256) ----
    {
        const int S = 256;
        std::vector<uint8_t> px(S * S * 4);
        int shingleH = 20, shingleW = 24;
        for (int y = 0; y < S; y++) {
            for (int x = 0; x < S; x++) {
                int row = y / shingleH;
                int xOff = (row % 2 == 0) ? 0 : shingleW / 2;
                int lx = (x + xOff) % shingleW;
                int ly = y % shingleH;
                bool edge = (lx < 1 || ly < 1);
                float n = hashNoise(x, y, 99) * 0.05f;
                float r, g, b;
                if (edge) {
                    r = 0.20f + n; g = 0.18f + n; b = 0.16f + n;
                } else {
                    float rowVar = hashNoise(row, (x + xOff) / shingleW, 33) * 0.06f;
                    // Dark slate gray
                    r = 0.30f + rowVar + n; g = 0.28f + rowVar + n; b = 0.32f + rowVar + n;
                }
                int idx = (y * S + x) * 4;
                px[idx] = clampF(r); px[idx+1] = clampF(g); px[idx+2] = clampF(b); px[idx+3] = 255;
            }
        }
        Resource<Texture> res; res.name = "Buildings/roof";
        if (res.data.CreateFromData(m_device, px.data(), S, S)) {
            m_textures["Buildings/roof"] = std::move(res); count++;
        }
    }

    // ---- Tree Bark Texture (128x128) ----
    {
        const int S = 128;
        std::vector<uint8_t> px(S * S * 4);
        for (int y = 0; y < S; y++) {
            for (int x = 0; x < S; x++) {
                float n1 = hashNoise(x, y, 55) * 0.08f;
                float n2 = hashNoise(x / 4, y, 77) * 0.10f;
                // Vertical bark grain
                float grain = (float)((x * 7 + y * 3) % 13) / 13.0f * 0.06f;
                float r = 0.35f + n1 + n2 + grain;
                float g = 0.24f + n1 * 0.7f + n2 * 0.5f + grain * 0.5f;
                float b = 0.14f + n1 * 0.3f + grain * 0.3f;
                int idx = (y * S + x) * 4;
                px[idx] = clampF(r); px[idx+1] = clampF(g); px[idx+2] = clampF(b); px[idx+3] = 255;
            }
        }
        Resource<Texture> res; res.name = "Buildings/tree";
        if (res.data.CreateFromData(m_device, px.data(), S, S)) {
            m_textures["Buildings/tree"] = std::move(res); count++;
        }
    }

    // ---- Fence Wood Plank Texture (128x256) ----
    {
        const int W = 128, H = 256;
        std::vector<uint8_t> px(W * H * 4);
        int plankW = 32;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int plank = x / plankW;
                int lx = x % plankW;
                bool gap = (lx < 1 || lx >= plankW - 1);
                float n = hashNoise(x, y, 123) * 0.05f;
                float grain = hashNoise(x / 2, y, 200 + plank) * 0.04f;
                float plankVar = hashNoise(plank, 0, 300) * 0.06f;
                float r, g, b;
                if (gap) {
                    r = 0.15f; g = 0.12f; b = 0.08f;
                } else {
                    r = 0.50f + plankVar + grain + n;
                    g = 0.36f + plankVar * 0.7f + grain + n * 0.8f;
                    b = 0.20f + plankVar * 0.3f + n * 0.5f;
                }
                int idx = (y * W + x) * 4;
                px[idx] = clampF(r); px[idx+1] = clampF(g); px[idx+2] = clampF(b); px[idx+3] = 255;
            }
        }
        Resource<Texture> res; res.name = "Buildings/fence";
        if (res.data.CreateFromData(m_device, px.data(), W, H)) {
            m_textures["Buildings/fence"] = std::move(res); count++;
        }
    }

    // ---- Stone Floor Tile Texture (256x256) ----
    {
        const int S = 256;
        std::vector<uint8_t> px(S * S * 4);
        int tileSize = 32, groutW = 2;
        for (int y = 0; y < S; y++) {
            for (int x = 0; x < S; x++) {
                int lx = x % tileSize, ly = y % tileSize;
                bool grout = (lx < groutW || ly < groutW);
                float n = hashNoise(x, y, 171) * 0.06f;
                float r, g, b;
                if (grout) {
                    r = 0.28f + n; g = 0.27f + n; b = 0.25f + n;
                } else {
                    int tx = x / tileSize, ty = y / tileSize;
                    float tileVar = hashNoise(tx, ty, 500) * 0.07f;
                    r = 0.52f + tileVar + n; g = 0.50f + tileVar + n; b = 0.46f + tileVar + n;
                }
                int idx = (y * S + x) * 4;
                px[idx] = clampF(r); px[idx+1] = clampF(g); px[idx+2] = clampF(b); px[idx+3] = 255;
            }
        }
        Resource<Texture> res; res.name = "Buildings/floor";
        if (res.data.CreateFromData(m_device, px.data(), S, S)) {
            m_textures["Buildings/floor"] = std::move(res); count++;
        }
    }

    LOG_INFO("Created %d total procedural textures", count);
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
