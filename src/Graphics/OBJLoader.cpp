#include "OBJLoader.h"
#include "Util/Log.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace WT {

using namespace DirectX;

// ============================================================
// Parse face vertex token: v, v/vt, v/vt/vn, v//vn
// ============================================================

OBJLoader::FaceIndex OBJLoader::ParseFaceVertex(const char* token) {
    FaceIndex fi;
    // Count slashes
    const char* s1 = strchr(token, '/');
    if (!s1) {
        // Just vertex index
        fi.v = atoi(token) - 1;
        return fi;
    }

    fi.v = atoi(token) - 1;

    const char* afterSlash1 = s1 + 1;
    const char* s2 = strchr(afterSlash1, '/');

    if (!s2) {
        // v/vt
        if (*afterSlash1 != '\0')
            fi.vt = atoi(afterSlash1) - 1;
        return fi;
    }

    // v/vt/vn or v//vn
    if (afterSlash1 != s2)
        fi.vt = atoi(afterSlash1) - 1;

    fi.vn = atoi(s2 + 1) - 1;
    return fi;
}

// ============================================================
// Load OBJ (wide string path)
// ============================================================

OBJLoadResult OBJLoader::Load(const std::wstring& filepath, const XMFLOAT4& defaultColor) {
    OBJLoadResult result;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        result.error = "Failed to open OBJ file";
        LOG_ERROR("OBJLoader: Failed to open file");
        return result;
    }

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;

    // For deduplicating unique vertex combinations
    struct VertexKey {
        int v, vt, vn;
        bool operator==(const VertexKey& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
    };
    struct VertexKeyHash {
        size_t operator()(const VertexKey& k) const {
            size_t h = std::hash<int>()(k.v);
            h ^= std::hash<int>()(k.vt) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.vn) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn") {
            XMFLOAT3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (prefix == "vt") {
            XMFLOAT2 uv;
            iss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        }
        else if (prefix == "f") {
            // Read all face vertices (supports triangles and quads via fan triangulation)
            std::vector<FaceIndex> faceVerts;
            std::string token;
            while (iss >> token) {
                faceVerts.push_back(ParseFaceVertex(token.c_str()));
            }

            if (faceVerts.size() < 3) continue;

            // Fan triangulation: 0-1-2, 0-2-3, 0-3-4, etc.
            for (size_t i = 1; i + 1 < faceVerts.size(); i++) {
                FaceIndex tri[3] = { faceVerts[0], faceVerts[i], faceVerts[i + 1] };

                for (int j = 0; j < 3; j++) {
                    const auto& fi = tri[j];
                    VertexKey key = { fi.v, fi.vt, fi.vn };

                    auto it = vertexMap.find(key);
                    if (it != vertexMap.end()) {
                        result.indices.push_back(it->second);
                    } else {
                        VertexPosNormalColor vert = {};

                        // Position
                        if (fi.v >= 0 && fi.v < static_cast<int>(positions.size())) {
                            vert.Position = positions[fi.v];
                        }

                        // Normal
                        if (fi.vn >= 0 && fi.vn < static_cast<int>(normals.size())) {
                            vert.Normal = normals[fi.vn];
                        }

                        // Texture coordinates
                        if (fi.vt >= 0 && fi.vt < static_cast<int>(texcoords.size())) {
                            vert.TexCoord = texcoords[fi.vt];
                        } else {
                            vert.TexCoord = { 0.0f, 0.0f };
                        }

                        // Color (default — entity ObjectColor will override)
                        vert.Color = defaultColor;

                        uint32_t idx = static_cast<uint32_t>(result.vertices.size());
                        result.vertices.push_back(vert);
                        result.indices.push_back(idx);
                        vertexMap[key] = idx;
                    }
                }
                result.triangleCount++;
            }
        }
        // Ignore: mtllib, usemtl, s, g, o — single-color mode skips these
    }

    // If no normals in file, compute flat normals per triangle
    if (normals.empty() && result.triangleCount > 0) {
        // Reset and rebuild with flat normals
        for (size_t i = 0; i + 2 < result.indices.size(); i += 3) {
            auto& v0 = result.vertices[result.indices[i]];
            auto& v1 = result.vertices[result.indices[i + 1]];
            auto& v2 = result.vertices[result.indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.Position);
            XMVECTOR p1 = XMLoadFloat3(&v1.Position);
            XMVECTOR p2 = XMLoadFloat3(&v2.Position);

            XMVECTOR edge1 = XMVectorSubtract(p1, p0);
            XMVECTOR edge2 = XMVectorSubtract(p2, p0);
            XMVECTOR normal = XMVector3Normalize(XMVector3Cross(edge1, edge2));

            XMFLOAT3 n;
            XMStoreFloat3(&n, normal);
            v0.Normal = n;
            v1.Normal = n;
            v2.Normal = n;
        }
    }

    result.success = true;
    LOG_INFO("OBJLoader: loaded %d vertices, %d triangles",
             static_cast<int>(result.vertices.size()), result.triangleCount);
    return result;
}

// ============================================================
// Load OBJ (narrow string path — converts to wide)
// ============================================================

OBJLoadResult OBJLoader::Load(const std::string& filepath, const XMFLOAT4& defaultColor) {
    std::wstring wide(filepath.begin(), filepath.end());
    return Load(wide, defaultColor);
}

// ============================================================
// Load OBJ with per-material vertex colors
// ============================================================

OBJLoadResult OBJLoader::LoadWithMaterials(const std::wstring& filepath,
                                            const MaterialColorMap& materialColors,
                                            const XMFLOAT4& defaultColor) {
    OBJLoadResult result;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        result.error = "Failed to open OBJ file";
        LOG_ERROR("OBJLoader: Failed to open file");
        return result;
    }

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;
    std::string currentMaterial;
    XMFLOAT4 currentColor = defaultColor;

    struct VertexKey {
        int v, vt, vn;
        std::string mat;
        bool operator==(const VertexKey& o) const {
            return v == o.v && vt == o.vt && vn == o.vn && mat == o.mat;
        }
    };
    struct VertexKeyHash {
        size_t operator()(const VertexKey& k) const {
            size_t h = std::hash<int>()(k.v);
            h ^= std::hash<int>()(k.vt) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.vn) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>()(k.mat) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn") {
            XMFLOAT3 n;
            iss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (prefix == "vt") {
            XMFLOAT2 uv;
            iss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        }
        else if (prefix == "usemtl") {
            iss >> currentMaterial;
            auto it = materialColors.find(currentMaterial);
            currentColor = (it != materialColors.end()) ? it->second : defaultColor;
        }
        else if (prefix == "f") {
            std::vector<FaceIndex> faceVerts;
            std::string token;
            while (iss >> token) {
                faceVerts.push_back(ParseFaceVertex(token.c_str()));
            }

            if (faceVerts.size() < 3) continue;

            for (size_t i = 1; i + 1 < faceVerts.size(); i++) {
                FaceIndex tri[3] = { faceVerts[0], faceVerts[i], faceVerts[i + 1] };

                for (int j = 0; j < 3; j++) {
                    const auto& fi = tri[j];
                    VertexKey key = { fi.v, fi.vt, fi.vn, currentMaterial };

                    auto it = vertexMap.find(key);
                    if (it != vertexMap.end()) {
                        result.indices.push_back(it->second);
                    } else {
                        VertexPosNormalColor vert = {};

                        if (fi.v >= 0 && fi.v < static_cast<int>(positions.size()))
                            vert.Position = positions[fi.v];
                        if (fi.vn >= 0 && fi.vn < static_cast<int>(normals.size()))
                            vert.Normal = normals[fi.vn];
                        if (fi.vt >= 0 && fi.vt < static_cast<int>(texcoords.size()))
                            vert.TexCoord = texcoords[fi.vt];
                        else
                            vert.TexCoord = { 0.0f, 0.0f };

                        vert.Color = currentColor;

                        uint32_t idx = static_cast<uint32_t>(result.vertices.size());
                        result.vertices.push_back(vert);
                        result.indices.push_back(idx);
                        vertexMap[key] = idx;
                    }
                }
                result.triangleCount++;
            }
        }
    }

    // Compute flat normals if none in file
    if (normals.empty() && result.triangleCount > 0) {
        for (size_t i = 0; i + 2 < result.indices.size(); i += 3) {
            auto& v0 = result.vertices[result.indices[i]];
            auto& v1 = result.vertices[result.indices[i + 1]];
            auto& v2 = result.vertices[result.indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.Position);
            XMVECTOR p1 = XMLoadFloat3(&v1.Position);
            XMVECTOR p2 = XMLoadFloat3(&v2.Position);

            XMVECTOR edge1 = XMVectorSubtract(p1, p0);
            XMVECTOR edge2 = XMVectorSubtract(p2, p0);
            XMVECTOR normal = XMVector3Normalize(XMVector3Cross(edge1, edge2));

            XMFLOAT3 n;
            XMStoreFloat3(&n, normal);
            v0.Normal = n;
            v1.Normal = n;
            v2.Normal = n;
        }
    }

    result.success = true;
    LOG_INFO("OBJLoader: loaded %d vertices, %d triangles (with materials)",
             static_cast<int>(result.vertices.size()), result.triangleCount);
    return result;
}

} // namespace WT
