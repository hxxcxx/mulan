#include "detail/dx11_buffer.h"

namespace mulan::engine {

DX11Buffer::DX11Buffer(const BufferDesc& desc, ID3D11Device* device, ID3D11DeviceContext* ctx)
    : m_desc(desc), m_ctx(ctx) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = desc.size;

    // Bind flags
    if (desc.bindFlags & BufferBindFlags::VertexBuffer)
        bd.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (desc.bindFlags & BufferBindFlags::IndexBuffer)
        bd.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (desc.bindFlags & BufferBindFlags::UniformBuffer)
        bd.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    if (desc.bindFlags & BufferBindFlags::ShaderResource)
        bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (desc.bindFlags & BufferBindFlags::UnorderedAccess)
        bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    // Usage mapping
    switch (desc.usage) {
    case BufferUsage::Immutable:
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.CPUAccessFlags = 0;
        break;
    case BufferUsage::Default:
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.CPUAccessFlags = 0;
        break;
    case BufferUsage::Dynamic:
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        break;
    case BufferUsage::Staging:
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        bd.BindFlags = 0;  // staging buffer cannot have bind flags
        break;
    }

    D3D11_SUBRESOURCE_DATA initData = {};
    D3D11_SUBRESOURCE_DATA* pInit = nullptr;
    if (desc.initData) {
        initData.pSysMem = desc.initData;
        pInit = &initData;
    }

    HRESULT hr = device->CreateBuffer(&bd, pInit, &m_buffer);
    DX11_CHECK(hr);
}

void DX11Buffer::update(uint32_t offset, uint32_t size, const void* data) {
    if (m_desc.usage == BufferUsage::Dynamic) {
        // Map / Discard
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        DX11_CHECK(hr);
        if (SUCCEEDED(hr)) {
            memcpy(static_cast<uint8_t*>(mapped.pData) + offset, data, size);
            m_ctx->Unmap(m_buffer.Get(), 0);
        }
    } else if (m_desc.usage == BufferUsage::Default) {
        // UpdateSubresource
        D3D11_BOX box = {};
        box.left = offset;
        box.right = offset + size;
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        m_ctx->UpdateSubresource(m_buffer.Get(), 0, &box, data, 0, 0);
    }
}

bool DX11Buffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (m_desc.usage != BufferUsage::Staging)
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return false;

    memcpy(outData, static_cast<uint8_t*>(mapped.pData) + offset, size);
    m_ctx->Unmap(m_buffer.Get(), 0);
    return true;
}

}  // namespace mulan::engine
