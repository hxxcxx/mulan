#include "detail/dx11_buffer.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {

// ID3D11DeviceContext1::*SetConstantBuffers1 的 offset / size 窗口要求
// 16 个 16-byte constants 对齐，即 256B。
constexpr uint32_t kConstantBufferAlignment = 256;

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

namespace mulan::engine {

DX11Buffer::DX11Buffer(const BufferDesc& desc, ID3D11Device* device, ID3D11DeviceContext* ctx)
    : m_desc(desc), m_ctx(ctx) {
    if (!device || !ctx)
        throw std::invalid_argument("DX11Buffer requires a valid device and immediate context");
    if (desc.size == 0)
        throw std::invalid_argument("DX11Buffer size must be greater than zero");
    if (desc.usage == BufferUsage::Immutable && !desc.initData)
        throw std::invalid_argument("D3D11 immutable buffers require initial data");

    D3D11_BUFFER_DESC bd = {};
    const bool isUniform = desc.bindFlags & BufferBindFlags::UniformBuffer;
    if (isUniform && desc.size > UINT32_MAX - (kConstantBufferAlignment - 1u))
        throw std::invalid_argument("DX11 uniform buffer size overflows alignment");
    m_byteWidth = isUniform ? alignUp(desc.size, kConstantBufferAlignment) : desc.size;
    bd.ByteWidth = m_byteWidth;

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
    if (desc.bindFlags & BufferBindFlags::IndirectBuffer)
        bd.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

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
        // UniformBuffer 会频繁更新局部范围（对象/材质槽）。WRITE_DISCARD 会
        // 丢弃整块资源，故内部使用 DEFAULT + UpdateSubresource 保持所有槽位。
        if (isUniform) {
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.CPUAccessFlags = 0;
        } else {
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            m_dynamicShadow.resize(m_byteWidth, 0);
            if (desc.initData)
                std::memcpy(m_dynamicShadow.data(), desc.initData, desc.size);
        }
        break;
    case BufferUsage::Staging:
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        if (bd.BindFlags != 0)
            throw std::invalid_argument("D3D11 staging buffers cannot have bind flags");
        break;
    }

    m_nativeUsage = bd.Usage;

    // D3D11.0 不支持超过 64KiB 的常量缓冲资源。没有 DeviceContext1 时，
    // CommandList 会把所需范围复制到 64KiB 的兼容常量缓冲；源缓冲无需带
    // D3D11_BIND_CONSTANT_BUFFER，因而仍可保存整个 Object UBO。
    if (isUniform && m_byteWidth > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16) {
        ComPtr<ID3D11DeviceContext1> context1;
        if (FAILED(ctx->QueryInterface(IID_PPV_ARGS(&context1))))
            bd.BindFlags &= ~D3D11_BIND_CONSTANT_BUFFER;
    }

    D3D11_SUBRESOURCE_DATA initData = {};
    D3D11_SUBRESOURCE_DATA* pInit = nullptr;
    std::vector<uint8_t> paddedUniformData;
    if (isUniform) {
        // D3D11.1 的范围绑定按 256B 对齐。用零初始化的填充区避免着色器在
        // 对齐窗口末尾读取未定义数据。
        paddedUniformData.resize(m_byteWidth, 0);
        if (desc.initData)
            std::memcpy(paddedUniformData.data(), desc.initData, desc.size);
        initData.pSysMem = paddedUniformData.data();
        pInit = &initData;
    } else if (desc.initData) {
        initData.pSysMem = desc.initData;
        pInit = &initData;
    }

    DX11_CHECK(device->CreateBuffer(&bd, pInit, &m_buffer));
}

void DX11Buffer::update(uint32_t offset, uint32_t size, const void* data) {
    if (!m_buffer || !data || size == 0 || offset > m_desc.size || size > m_desc.size - offset) {
        std::fprintf(stderr, "[DX11Buffer] invalid update range (offset=%u, size=%u, buffer=%u)\n", offset, size,
                     m_desc.size);
        return;
    }
    if (m_desc.usage == BufferUsage::Immutable) {
        std::fprintf(stderr, "[DX11Buffer] immutable buffer cannot be updated\n");
        return;
    }

    if (m_nativeUsage == D3D11_USAGE_DYNAMIC) {
        // 动态顶点/索引缓冲也保留 CPU 镜像后整块 DISCARD 上传，避免局部更新
        // 覆盖未修改的数据。
        std::memcpy(m_dynamicShadow.data() + offset, data, size);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            logDX11Failure(hr, "ID3D11DeviceContext::Map(dynamic buffer)");
            return;
        }
        std::memcpy(mapped.pData, m_dynamicShadow.data(), m_byteWidth);
        m_ctx->Unmap(m_buffer.Get(), 0);
    } else if (m_nativeUsage == D3D11_USAGE_DEFAULT) {
        D3D11_BOX box = {};
        box.left = offset;
        box.right = offset + size;
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        m_ctx->UpdateSubresource(m_buffer.Get(), 0, &box, data, 0, 0);
    } else if (m_nativeUsage == D3D11_USAGE_STAGING) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(hr)) {
            logDX11Failure(hr, "ID3D11DeviceContext::Map(staging buffer)");
            return;
        }
        std::memcpy(static_cast<uint8_t*>(mapped.pData) + offset, data, size);
        m_ctx->Unmap(m_buffer.Get(), 0);
    }
}

bool DX11Buffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (m_nativeUsage != D3D11_USAGE_STAGING || !m_buffer || !outData || size == 0 || offset > m_desc.size ||
        size > m_desc.size - offset)
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return false;

    std::memcpy(outData, static_cast<uint8_t*>(mapped.pData) + offset, size);
    m_ctx->Unmap(m_buffer.Get(), 0);
    return true;
}

}  // namespace mulan::engine
