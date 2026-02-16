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

void DebugRenderer::DrawRotatedBox(const XMFLOAT3& center, const XMFLOAT3& halfExt,
                                    const XMFLOAT3X3& rotMat, const XMFLOAT4& color) {
    // OBB local axes (rows of the rotation matrix)
    XMFLOAT3 axisX = { rotMat._11, rotMat._12, rotMat._13 };
    XMFLOAT3 axisY = { rotMat._21, rotMat._22, rotMat._23 };
    XMFLOAT3 axisZ = { rotMat._31, rotMat._32, rotMat._33 };

    // Scale axes by half-extents
    XMFLOAT3 ex = { axisX.x * halfExt.x, axisX.y * halfExt.x, axisX.z * halfExt.x };
    XMFLOAT3 ey = { axisY.x * halfExt.y, axisY.y * halfExt.y, axisY.z * halfExt.y };
    XMFLOAT3 ez = { axisZ.x * halfExt.z, axisZ.y * halfExt.z, axisZ.z * halfExt.z };

    // 8 corners of the oriented box
    auto add3 = [](XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 d) -> XMFLOAT3 {
        return { a.x + b.x + c.x + d.x, a.y + b.y + c.y + d.y, a.z + b.z + c.z + d.z };
    };
    auto neg = [](XMFLOAT3 v) -> XMFLOAT3 { return { -v.x, -v.y, -v.z }; };

    // corners: center ± ex ± ey ± ez
    XMFLOAT3 c[8] = {
        add3(center, neg(ex), neg(ey), neg(ez)),  // 0: ---
        add3(center, ex,      neg(ey), neg(ez)),  // 1: +--
        add3(center, ex,      ey,      neg(ez)),  // 2: ++-
        add3(center, neg(ex), ey,      neg(ez)),  // 3: -+-
        add3(center, neg(ex), neg(ey), ez),        // 4: --+
        add3(center, ex,      neg(ey), ez),        // 5: +-+
        add3(center, ex,      ey,      ez),        // 6: +++
        add3(center, neg(ex), ey,      ez),        // 7: -++
    };

    // Bottom face (0-1-5-4)
    DrawLine(c[0], c[1], color);
    DrawLine(c[1], c[5], color);
    DrawLine(c[5], c[4], color);
    DrawLine(c[4], c[0], color);

    // Top face (3-2-6-7)
    DrawLine(c[3], c[2], color);
    DrawLine(c[2], c[6], color);
    DrawLine(c[6], c[7], color);
    DrawLine(c[7], c[3], color);

    // Vertical edges
    DrawLine(c[0], c[3], color);
    DrawLine(c[1], c[2], color);
    DrawLine(c[5], c[6], color);
    DrawLine(c[4], c[7], color);
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
