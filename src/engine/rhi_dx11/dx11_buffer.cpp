#include "detail/dx11_buffer.h"
#include "../rhi/engine_error_code.h"

#include <cstdio>
#include <cstring>

namespace {

// ID3D11DeviceContext1::*SetConstantBuffers1 的 offset / size 窗口要求
// 16 个 16-byte constants 对齐，即 256B。
constexpr uint32_t kConstantBufferAlignment = 256;

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

namespace mulan::engine {

DX11Buffer::~DX11Buffer() {
    waitForLastUseBeforeDestruction();
}

DX11Buffer::DX11Buffer(uint32_t transientUniformPageSize, ID3D11Device* device, ID3D11DeviceContext* ctx)
    : m_desc(BufferDesc::uniform(transientUniformPageSize, "DX11TransientUniformPage")),
      m_ctx(ctx),
      m_byteWidth(transientUniformPageSize),
      m_nativeUsage(D3D11_USAGE_DYNAMIC),
      m_transientUniformPage(true) {
    if (!device || !ctx || transientUniformPageSize == 0 || (transientUniformPageSize % 16u) != 0)
        return;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = transientUniformPageSize;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const auto result = checkDX11(device->CreateBuffer(&desc, nullptr, &m_buffer),
                                  "ID3D11Device::CreateBuffer(transient uniform page)");
    if (!result)
        m_buffer.Reset();
}

std::unique_ptr<DX11Buffer> DX11Buffer::createTransientUniformPage(uint32_t size, ID3D11Device* device,
                                                                   ID3D11DeviceContext* ctx) {
    auto page = std::unique_ptr<DX11Buffer>(new DX11Buffer(size, device, ctx));
    if (!page->isValid())
        return nullptr;
    return page;
}

DX11Buffer::DX11Buffer(const BufferDesc& desc, ID3D11Device* device, ID3D11DeviceContext* ctx)
    : m_desc(desc), m_ctx(ctx) {
    if (!device || !ctx || desc.size == 0 || (desc.usage == BufferUsage::Immutable && !desc.initData)) {
        LOG_ERROR("[DX11] Buffer initialization rejected: invalid arguments");
        return;
    }

    D3D11_BUFFER_DESC bd = {};
    const bool isUniform = desc.bindFlags & BufferBindFlags::UniformBuffer;
    if (isUniform && desc.size > UINT32_MAX - (kConstantBufferAlignment - 1u)) {
        LOG_ERROR("[DX11] Buffer initialization rejected: uniform size overflows alignment");
        return;
    }
    m_byteWidth = isUniform ? alignUp(desc.size, kConstantBufferAlignment) : desc.size;
    bd.ByteWidth = m_byteWidth;

    // 资源绑定用途
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

    // CPU/GPU 访问方式
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
        if (bd.BindFlags != 0) {
            LOG_ERROR("[DX11] Buffer initialization rejected: staging buffers cannot have bind flags");
            return;
        }
        break;
    }

    m_nativeUsage = bd.Usage;

    // D3D11 常量缓冲上限为 64 KiB。更大的静态 Uniform 缓冲作为普通源缓冲保存，
    // 绑定时由兼容路径复制当前范围。
    if (isUniform && m_byteWidth > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16) {
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
        m_uniformShadow = paddedUniformData;
    } else if (desc.initData) {
        initData.pSysMem = desc.initData;
        pInit = &initData;
    }

    if (!checkDX11(device->CreateBuffer(&bd, pInit, &m_buffer), "ID3D11Device::CreateBuffer"))
        return;
    m_desc.discardInitialData();
}

Result<void> DX11Buffer::write(uint32_t offset, uint32_t size, const void* data) {
    if (auto wait = waitForLastUse(); !wait)
        return std::unexpected(wait.error());
    if (!m_buffer || !data || size == 0 || offset > m_desc.size || size > m_desc.size - offset) {
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX11 buffer write range is invalid"));
    }
    if (m_desc.usage != BufferUsage::Dynamic)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX11 buffer write requires a Dynamic buffer"));

    // CPU 镜像仅供不支持原生范围绑定的兼容路径读取；原生缓冲仍同步更新，
    // D3D11.1 可以直接绑定其中的 Uniform 范围。
    if (!m_uniformShadow.empty()) {
        std::memcpy(m_uniformShadow.data() + offset, data, size);
        ++m_uniformVersion;
        if (m_uniformVersion == 0)
            m_uniformVersion = 1;

        // D3D11 不允许对原生常量缓冲执行局部 UpdateSubresource，使用镜像补全未修改区域。
        m_ctx->UpdateSubresource(m_buffer.Get(), 0, nullptr, m_uniformShadow.data(), 0, 0);
        return {};
    }

    if (m_nativeUsage == D3D11_USAGE_DYNAMIC) {
        // 动态顶点/索引缓冲也保留 CPU 镜像后整块 DISCARD 上传，避免局部更新
        // 覆盖未修改的数据。
        std::memcpy(m_dynamicShadow.data() + offset, data, size);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            logDX11Failure(hr, "ID3D11DeviceContext::Map(dynamic buffer)");
            return std::unexpected(
                    makeError(EngineErrorCode::ResourceUploadFailed, "DX11 dynamic buffer mapping failed"));
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
    }
    return {};
}

const void* DX11Buffer::uniformData(uint32_t offset, uint32_t size) const {
    if (m_uniformShadow.empty() || size == 0 || offset > m_uniformShadow.size() ||
        size > m_uniformShadow.size() - offset) {
        return nullptr;
    }
    return m_uniformShadow.data() + offset;
}

Result<void> DX11Buffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (auto wait = waitForLastUse(); !wait)
        return std::unexpected(wait.error());
    if (m_nativeUsage != D3D11_USAGE_STAGING || !m_buffer || !outData || size == 0 || offset > m_desc.size ||
        size > m_desc.size - offset)
        return std::unexpected(makeError(EngineErrorCode::ResourceReadbackFailed,
                                         "DX11 buffer readback requires a valid staging range"));

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(m_buffer.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceReadbackFailed, "DX11 staging buffer mapping failed"));

    std::memcpy(outData, static_cast<uint8_t*>(mapped.pData) + offset, size);
    m_ctx->Unmap(m_buffer.Get(), 0);
    return {};
}

}  // namespace mulan::engine
