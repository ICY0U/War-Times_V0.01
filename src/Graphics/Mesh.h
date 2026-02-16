#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <DirectXMath.h>

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

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

    // Bounds (local space, computed from vertex positions)
    const XMFLOAT3& GetBoundsMin() const { return m_boundsMin; }
    const XMFLOAT3& GetBoundsMax() const { return m_boundsMax; }
    XMFLOAT3 GetBoundsCenter() const {
        return { (m_boundsMin.x + m_boundsMax.x) * 0.5f,
                 (m_boundsMin.y + m_boundsMax.y) * 0.5f,
                 (m_boundsMin.z + m_boundsMax.z) * 0.5f };
    }
    XMFLOAT3 GetBoundsHalfExtent() const {
        return { (m_boundsMax.x - m_boundsMin.x) * 0.5f,
                 (m_boundsMax.y - m_boundsMin.y) * 0.5f,
                 (m_boundsMax.z - m_boundsMin.z) * 0.5f };
    }
    bool HasBounds() const { return m_hasBounds; }

    // Set bounds from vertex data (called by MeshLoader)
    void SetBounds(const XMFLOAT3& mn, const XMFLOAT3& mx) {
        m_boundsMin = mn; m_boundsMax = mx; m_hasBounds = true;
    }

    void Release();

private:
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    UINT m_vertexCount  = 0;
    UINT m_indexCount   = 0;
    UINT m_vertexStride = 0;
    XMFLOAT3 m_boundsMin = { 0,0,0 };
    XMFLOAT3 m_boundsMax = { 0,0,0 };
    bool m_hasBounds = false;
};

} // namespace WT
