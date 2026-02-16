#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include "Shader.h"

namespace WT {

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DebugRenderer {
public:
    bool Init(ID3D11Device* device, const std::wstring& shaderDir);
    void Shutdown();

    // Queue world-space line primitives (accumulated until Flush)
    void DrawLine(const XMFLOAT3& from, const XMFLOAT3& to, const XMFLOAT4& color);
    void DrawBox(const XMFLOAT3& center, const XMFLOAT3& extents, const XMFLOAT4& color);
    void DrawRotatedBox(const XMFLOAT3& center, const XMFLOAT3& halfExt,
                        const XMFLOAT3X3& rotMat, const XMFLOAT4& color);
    void DrawGrid(float size, int divisions, const XMFLOAT4& color);
    void DrawSphere(const XMFLOAT3& center, float radius, const XMFLOAT4& color, int segments = 16);
    void DrawAxis(const XMFLOAT3& origin, float length);
    void DrawFrustum(const XMFLOAT4X4& viewProj, const XMFLOAT4& color);

    // Draw all accumulated lines then clear
    void Flush(ID3D11DeviceContext* context);

    uint32_t GetLineCount() const { return static_cast<uint32_t>(m_vertices.size()) / 2; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool e) { m_enabled = e; }

private:
    struct DebugVertex {
        XMFLOAT3 Position;
        XMFLOAT4 Color;
    };

    std::vector<DebugVertex> m_vertices;

    ComPtr<ID3D11Buffer> m_vertexBuffer;
    UINT m_maxVertices  = 131072;   // 128K vertices = 64K lines
    UINT m_vertexStride = sizeof(DebugVertex);

    Shader m_shader;
    bool   m_enabled = true;
};

} // namespace WT
