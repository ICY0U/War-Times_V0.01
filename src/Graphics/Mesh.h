#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

namespace WT {

using Microsoft::WRL::ComPtr;

class Mesh {
public:
    // Create from raw vertex/index data
    template<typename V>
    bool Create(ID3D11Device* device, const std::vector<V>& vertices, const std::vector<UINT>& indices) {
        return Create(device, vertices.data(), static_cast<UINT>(vertices.size()),
                      sizeof(V), indices.data(), static_cast<UINT>(indices.size()));
    }

    bool Create(ID3D11Device* device,
                const void* vertexData, UINT vertexCount, UINT vertexStride,
                const UINT* indexData, UINT indexCount);

    void Draw(ID3D11DeviceContext* context) const;

    UINT GetIndexCount() const { return m_indexCount; }
    UINT GetVertexCount() const { return m_vertexCount; }
    bool IsValid() const { return m_vertexBuffer && m_indexBuffer; }

    void Release();

private:
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_vertexCount  = 0;
    UINT m_indexCount   = 0;
    UINT m_vertexStride = 0;
};

} // namespace WT
