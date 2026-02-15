#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Util/Log.h"

namespace WT {

using Microsoft::WRL::ComPtr;

template<typename T>
class ConstantBuffer {
public:
    bool Init(ID3D11Device* device) {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth      = sizeof(T);
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device->CreateBuffer(&bd, nullptr, m_buffer.GetAddressOf());
        HR_CHECK(hr, "CreateBuffer (constant buffer)");
        return true;
    }

    bool Update(ID3D11DeviceContext* context, const T& data) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return false;
        memcpy(mapped.pData, &data, sizeof(T));
        context->Unmap(m_buffer.Get(), 0);
        return true;
    }

    void BindVS(ID3D11DeviceContext* context, UINT slot) const {
        context->VSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindPS(ID3D11DeviceContext* context, UINT slot) const {
        context->PSSetConstantBuffers(slot, 1, m_buffer.GetAddressOf());
    }

    void BindBoth(ID3D11DeviceContext* context, UINT slot) const {
        BindVS(context, slot);
        BindPS(context, slot);
    }

    ID3D11Buffer* GetBuffer() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11Buffer> m_buffer;
};

} // namespace WT
