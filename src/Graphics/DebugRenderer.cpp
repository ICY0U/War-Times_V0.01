#include "DebugRenderer.h"
#include "Util/Log.h"
#include "Util/MathHelpers.h"
#include <algorithm>
#include <cmath>

namespace WT {

bool DebugRenderer::Init(ID3D11Device* device, const std::wstring& shaderDir) {
    std::wstring vsPath = shaderDir + L"DebugVS.hlsl";
    std::wstring psPath = shaderDir + L"DebugPS.hlsl";

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    if (!m_shader.LoadVS(device, vsPath, "VSMain", layout, 2)) return false;
    if (!m_shader.LoadPS(device, psPath, "PSMain")) return false;

    // Create dynamic vertex buffer
    D3D11_BUFFER_DESC bd = {};
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth      = m_maxVertices * sizeof(DebugVertex);
    bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&bd, nullptr, m_vertexBuffer.GetAddressOf());
    HR_CHECK(hr, "CreateBuffer (debug vertex buffer)");

    LOG_INFO("Debug renderer initialized (max %u lines)", m_maxVertices / 2);
    return true;
}

void DebugRenderer::Shutdown() {
    m_vertexBuffer.Reset();
    m_vertices.clear();
    LOG_INFO("Debug renderer shutdown");
}

// ---- Primitive Drawing ----

void DebugRenderer::DrawLine(const XMFLOAT3& from, const XMFLOAT3& to, const XMFLOAT4& color) {
    m_vertices.push_back({ from, color });
    m_vertices.push_back({ to,   color });
}

void DebugRenderer::DrawBox(const XMFLOAT3& center, const XMFLOAT3& extents, const XMFLOAT4& color) {
    float x0 = center.x - extents.x, x1 = center.x + extents.x;
    float y0 = center.y - extents.y, y1 = center.y + extents.y;
    float z0 = center.z - extents.z, z1 = center.z + extents.z;

    // Bottom face
    DrawLine({ x0, y0, z0 }, { x1, y0, z0 }, color);
    DrawLine({ x1, y0, z0 }, { x1, y0, z1 }, color);
    DrawLine({ x1, y0, z1 }, { x0, y0, z1 }, color);
    DrawLine({ x0, y0, z1 }, { x0, y0, z0 }, color);

    // Top face
    DrawLine({ x0, y1, z0 }, { x1, y1, z0 }, color);
    DrawLine({ x1, y1, z0 }, { x1, y1, z1 }, color);
    DrawLine({ x1, y1, z1 }, { x0, y1, z1 }, color);
    DrawLine({ x0, y1, z1 }, { x0, y1, z0 }, color);

    // Vertical edges
    DrawLine({ x0, y0, z0 }, { x0, y1, z0 }, color);
    DrawLine({ x1, y0, z0 }, { x1, y1, z0 }, color);
    DrawLine({ x1, y0, z1 }, { x1, y1, z1 }, color);
    DrawLine({ x0, y0, z1 }, { x0, y1, z1 }, color);
}

void DebugRenderer::DrawGrid(float size, int divisions, const XMFLOAT4& color) {
    float half = size * 0.5f;
    float step = size / static_cast<float>(divisions);

    for (int i = 0; i <= divisions; i++) {
        float t = -half + i * step;
        DrawLine({ t, 0.0f, -half }, { t, 0.0f, half }, color);   // Along Z
        DrawLine({ -half, 0.0f, t }, { half, 0.0f, t }, color);   // Along X
    }
}

void DebugRenderer::DrawSphere(const XMFLOAT3& center, float radius, const XMFLOAT4& color, int segments) {
    float step = TWO_PI / static_cast<float>(segments);

    for (int i = 0; i < segments; i++) {
        float a0 = i * step;
        float a1 = (i + 1) * step;

        // XY circle
        DrawLine(
            { center.x + cosf(a0) * radius, center.y + sinf(a0) * radius, center.z },
            { center.x + cosf(a1) * radius, center.y + sinf(a1) * radius, center.z },
            color);

        // XZ circle
        DrawLine(
            { center.x + cosf(a0) * radius, center.y, center.z + sinf(a0) * radius },
            { center.x + cosf(a1) * radius, center.y, center.z + sinf(a1) * radius },
            color);

        // YZ circle
        DrawLine(
            { center.x, center.y + cosf(a0) * radius, center.z + sinf(a0) * radius },
            { center.x, center.y + cosf(a1) * radius, center.z + sinf(a1) * radius },
            color);
    }
}

void DebugRenderer::DrawAxis(const XMFLOAT3& origin, float length) {
    DrawLine(origin, { origin.x + length, origin.y, origin.z }, { 1.0f, 0.0f, 0.0f, 1.0f }); // X = Red
    DrawLine(origin, { origin.x, origin.y + length, origin.z }, { 0.0f, 1.0f, 0.0f, 1.0f }); // Y = Green
    DrawLine(origin, { origin.x, origin.y, origin.z + length }, { 0.0f, 0.0f, 1.0f, 1.0f }); // Z = Blue
}

void DebugRenderer::DrawFrustum(const XMFLOAT4X4& viewProj, const XMFLOAT4& color) {
    // Compute frustum corners by inverting the view-projection matrix
    XMMATRIX invVP = XMMatrixInverse(nullptr, XMLoadFloat4x4(&viewProj));

    // NDC corners (left-handed, Z in [0,1])
    XMFLOAT3 ndcCorners[8] = {
        { -1, -1, 0 }, {  1, -1, 0 }, {  1,  1, 0 }, { -1,  1, 0 }, // Near
        { -1, -1, 1 }, {  1, -1, 1 }, {  1,  1, 1 }, { -1,  1, 1 }, // Far
    };

    XMFLOAT3 worldCorners[8];
    for (int i = 0; i < 8; i++) {
        XMVECTOR ndc = XMLoadFloat3(&ndcCorners[i]);
        XMVECTOR world = XMVector3TransformCoord(ndc, invVP);
        XMStoreFloat3(&worldCorners[i], world);
    }

    // Near plane
    DrawLine(worldCorners[0], worldCorners[1], color);
    DrawLine(worldCorners[1], worldCorners[2], color);
    DrawLine(worldCorners[2], worldCorners[3], color);
    DrawLine(worldCorners[3], worldCorners[0], color);

    // Far plane
    DrawLine(worldCorners[4], worldCorners[5], color);
    DrawLine(worldCorners[5], worldCorners[6], color);
    DrawLine(worldCorners[6], worldCorners[7], color);
    DrawLine(worldCorners[7], worldCorners[4], color);

    // Connecting edges
    DrawLine(worldCorners[0], worldCorners[4], color);
    DrawLine(worldCorners[1], worldCorners[5], color);
    DrawLine(worldCorners[2], worldCorners[6], color);
    DrawLine(worldCorners[3], worldCorners[7], color);
}

// ---- Flush ----

void DebugRenderer::Flush(ID3D11DeviceContext* context) {
    if (!m_enabled || m_vertices.empty()) return;

    UINT vertexCount = static_cast<UINT>(m_vertices.size());
    if (vertexCount > m_maxVertices) {
        LOG_WARN("Debug renderer: %u vertices exceeds max %u, clamping", vertexCount, m_maxVertices);
        vertexCount = m_maxVertices;
    }

    // Map dynamic buffer and upload accumulated vertices
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        m_vertices.clear();
        return;
    }
    memcpy(mapped.pData, m_vertices.data(), vertexCount * sizeof(DebugVertex));
    context->Unmap(m_vertexBuffer.Get(), 0);

    // Bind debug shader and draw
    m_shader.Bind(context);

    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_vertexStride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->Draw(vertexCount, 0);

    m_vertices.clear();
}

} // namespace WT
