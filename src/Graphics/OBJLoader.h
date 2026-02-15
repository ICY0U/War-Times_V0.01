#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "Util/MathHelpers.h"

namespace WT {

// ============================================================
// OBJ Loader — loads Wavefront .obj files into engine vertex format
// Supports: positions, normals, faces (triangulated)
// Supports per-material vertex colors via material color map
// ============================================================

struct OBJLoadResult {
    std::vector<VertexPosNormalColor> vertices;
    std::vector<uint32_t> indices;
    bool success = false;
    std::string error;
    int triangleCount = 0;
};

// Map of material name -> vertex color
using MaterialColorMap = std::unordered_map<std::string, DirectX::XMFLOAT4>;

class OBJLoader {
public:
    // Load an OBJ file. defaultColor is applied to all vertices.
    static OBJLoadResult Load(const std::wstring& filepath,
                               const DirectX::XMFLOAT4& defaultColor = { 0.6f, 0.6f, 0.6f, 1.0f });

    // Load from narrow string path
    static OBJLoadResult Load(const std::string& filepath,
                               const DirectX::XMFLOAT4& defaultColor = { 0.6f, 0.6f, 0.6f, 1.0f });

    // Load with per-material colors (material name -> color)
    static OBJLoadResult LoadWithMaterials(const std::wstring& filepath,
                                            const MaterialColorMap& materialColors,
                                            const DirectX::XMFLOAT4& defaultColor = { 0.6f, 0.6f, 0.6f, 1.0f });

private:
    // Parse a face index (v, v/vt, v/vt/vn, v//vn)
    struct FaceIndex {
        int v  = -1;
        int vt = -1;
        int vn = -1;
    };

    static FaceIndex ParseFaceVertex(const char* token);
};

} // namespace WT
