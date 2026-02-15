#include "Mesh.h"
#include "Util/Log.h"

namespace WT {

bool Mesh::Create(ID3D11Device* device,
                  const void* vertexData, UINT vertexCount, UINT vertexStride,
                  const UINT* indexData, UINT indexCount) {
    Release();

    m_vertexCount  = vertexCount;
    m_indexCount   = indexCount;
    m_vertexStride = vertexStride;

    // Vertex buffer
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage     = D3D11_USAGE_DEFAULT;
    vbd.ByteWidth = vertexStride * vertexCount;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vsd = {};
    vsd.pSysMem = vertexData;

    HRESULT hr = device->CreateBuffer(&vbd, &vsd, m_vertexBuffer.GetAddressOf());
    HR_CHECK(hr, "CreateBuffer (vertex buffer)");

    // Index buffer
    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage     = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = sizeof(UINT) * indexCount;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA isd = {};
    isd.pSysMem = indexData;

    hr = device->CreateBuffer(&ibd, &isd, m_indexBuffer.GetAddressOf());
    HR_CHECK(hr, "CreateBuffer (index buffer)");

    return true;
}

void Mesh::Draw(ID3D11DeviceContext* context) const {
    if (!m_vertexBuffer || !m_indexBuffer) return;

    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_vertexStride, &offset);
    context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->DrawIndexed(m_indexCount, 0, 0);
}

void Mesh::Release() {
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_vertexCount  = 0;
    m_indexCount   = 0;
    m_vertexStride = 0;
}

} // namespace WT
