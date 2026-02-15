#pragma once

#include <string>
#include <d3d11.h>

namespace WT {

class Mesh;

// ============================================================
// MeshLoader — loads .mesh binary files into GPU Mesh objects
//
// Binary format (v1):
//   [4 bytes]  Magic        "MESH" (0x4853454D)
//   [4 bytes]  Version      1
//   [4 bytes]  VertexCount  N
//   [4 bytes]  IndexCount   M
//   [N * 48]   Vertices     VertexPosNormalColor (pos3f + norm3f + color4f + uv2f)
//   [M * 4]    Indices      uint32
//
// All data is little-endian, pre-converted to left-handed Y-up
// coordinate system with CW winding (ready for D3D11).
// ============================================================
class MeshLoader {
public:
    // Load a single .mesh file, creating a GPU Mesh
    static bool LoadMesh(ID3D11Device* device, const std::wstring& filepath,
                         Mesh& outMesh);

    // Recursively scan a directory for .mesh files, register each with ResourceManager.
    // The mesh name is the relative path without extension (e.g. "Guns/Rifle").
    // Returns the number of meshes loaded.
    static int LoadDirectory(ID3D11Device* device, const std::wstring& dirPath);

    // Format constants
    static constexpr uint32_t MESH_MAGIC   = 0x4853454D; // "MESH"
    static constexpr uint32_t MESH_VERSION = 1;
};

} // namespace WT
