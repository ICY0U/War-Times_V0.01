#include "MeshLoader.h"
#include "Mesh.h"
#include "Core/ResourceManager.h"
#include "Util/Log.h"
#include "Util/MathHelpers.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <cfloat>
#include <algorithm>

namespace WT {

// ================================================================
// LoadMesh — read a .mesh binary file into a GPU Mesh
// ================================================================
bool MeshLoader::LoadMesh(ID3D11Device* device, const std::wstring& filepath,
                          Mesh& outMesh) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("MeshLoader: Cannot open file");
        return false;
    }

    // ---- Header ----
    uint32_t magic = 0, version = 0, vertexCount = 0, indexCount = 0;
    file.read(reinterpret_cast<char*>(&magic),       sizeof(magic));
    file.read(reinterpret_cast<char*>(&version),     sizeof(version));
    file.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
    file.read(reinterpret_cast<char*>(&indexCount),  sizeof(indexCount));

    if (!file) {
        LOG_ERROR("MeshLoader: Failed to read header");
        return false;
    }

    if (magic != MESH_MAGIC) {
        LOG_ERROR("MeshLoader: Invalid magic (expected 0x%08X, got 0x%08X)",
                  MESH_MAGIC, magic);
        return false;
    }

    if (version != MESH_VERSION) {
        LOG_ERROR("MeshLoader: Unsupported version %u (expected %u)",
                  version, MESH_VERSION);
        return false;
    }

    if (vertexCount == 0 || indexCount == 0) {
        LOG_ERROR("MeshLoader: Empty mesh (verts=%u, indices=%u)",
                  vertexCount, indexCount);
        return false;
    }

    // Sanity limits
    if (vertexCount > 10'000'000 || indexCount > 30'000'000) {
        LOG_ERROR("MeshLoader: Mesh too large (verts=%u, indices=%u)",
                  vertexCount, indexCount);
        return false;
    }

    // ---- Vertices ----
    std::vector<VertexPosNormalColor> vertices(vertexCount);
    file.read(reinterpret_cast<char*>(vertices.data()),
              vertexCount * sizeof(VertexPosNormalColor));
    if (!file) {
        LOG_ERROR("MeshLoader: Failed to read %u vertices", vertexCount);
        return false;
    }

    // ---- Indices ----
    std::vector<UINT> indices(indexCount);
    file.read(reinterpret_cast<char*>(indices.data()),
              indexCount * sizeof(UINT));
    if (!file) {
        LOG_ERROR("MeshLoader: Failed to read %u indices", indexCount);
        return false;
    }

    // ---- Compute bounds from vertex positions ----
    XMFLOAT3 mn = { FLT_MAX, FLT_MAX, FLT_MAX };
    XMFLOAT3 mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (UINT i = 0; i < vertexCount; i++) {
        const auto& v = vertices[i];
        mn.x = (std::min)(mn.x, v.Position.x);
        mn.y = (std::min)(mn.y, v.Position.y);
        mn.z = (std::min)(mn.z, v.Position.z);
        mx.x = (std::max)(mx.x, v.Position.x);
        mx.y = (std::max)(mx.y, v.Position.y);
        mx.z = (std::max)(mx.z, v.Position.z);
    }

    // ---- Create GPU buffers ----
    if (!outMesh.Create(device, vertices, indices)) {
        LOG_ERROR("MeshLoader: Failed to create GPU mesh");
        return false;
    }

    outMesh.SetBounds(mn, mx);
    LOG_INFO("MeshLoader: Loaded %u verts, %u indices (bounds [%.1f,%.1f,%.1f]-[%.1f,%.1f,%.1f])",
             vertexCount, indexCount, mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
    return true;
}

// ================================================================
// LoadDirectory — recursively find all .mesh files and register them
// ================================================================
int MeshLoader::LoadDirectory(ID3D11Device* device, const std::wstring& dirPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        LOG_WARN("MeshLoader: Models directory not found");
        return 0;
    }

    int count = 0;
    for (auto& entry : fs::recursive_directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().wstring();
        // Case-insensitive extension check
        if (ext != L".mesh" && ext != L".MESH") continue;

        // Build name: relative path from dirPath, forward slashes, no extension
        // e.g. "Guns/Rifle" or "PreFabs/Walls/BrickWall_01"
        fs::path relPath = fs::relative(entry.path(), dirPath);
        std::string meshName = relPath.replace_extension().generic_string();

        // Load mesh
        Mesh mesh;
        if (LoadMesh(device, entry.path().wstring(), mesh)) {
            ResourceManager::Get().RegisterMesh(meshName, std::move(mesh), entry.path());
            count++;
        } else {
            LOG_WARN("MeshLoader: Failed to load '%s'", meshName.c_str());
        }
    }

    LOG_INFO("MeshLoader: Loaded %d meshes from directory", count);
    return count;
}

} // namespace WT
