/**
 * @file DX12Buffer.cpp
 * @brief D3D12 缓冲区实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12Buffer.h"

namespace mulan::engine {

DX12Buffer::DX12Buffer(const BufferDesc& desc, ID3D12Device* device)
    : m_desc(desc)
{
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (desc.bindFlags & BufferBindFlags::UnorderedAccess) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    D3D12_RESOURCE_DESC resDesc = {};

    resDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment          = 0;
    resDesc.Width              = (desc.bindFlags & BufferBindFlags::UniformBuffer)
        ? ((static_cast<uint64_t>(desc.size) + 255ull) & ~255ull)
        : desc.size;
    resDesc.Height             = 1;
    resDesc.DepthOrArraySize   = 1;
    resDesc.MipLevels          = 1;
    resDesc.Format             = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count   = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags              = flags;

    switch (desc.usage) {
    case BufferUsage::Immutable:
    case BufferUsage::Default:
        heapProps.Type                 = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        initialState = D3D12_RESOURCE_STATE_COMMON;
        break;

    case BufferUsage::Dynamic:
        // 上传堆，CPU 可写
        heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;

    case BufferUsage::Staging:
        // 回读堆，CPU 可读写
        heapProps.Type                 = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    }

    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask  = 1;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &resDesc, initialState,
        nullptr, IID_PPV_ARGS(&m_resource));
    DX12_CHECK(hr);

    // Upload/Readback 堆立即映射
    if (desc.usage == BufferUsage::Dynamic || desc.usage == BufferUsage::Staging) {
        D3D12_RANGE range = { 0, 0 };  // 不读
        m_resource->Map(0, &range, &m_mappedData);
    }

    // 保存初始数据用于后续上传
    if (desc.initData && desc.size > 0) {
        m_pendingData.assign(
            static_cast<const uint8_t*>(desc.initData),
            static_cast<const uint8_t*>(desc.initData) + desc.size);
    }
}

DX12Buffer::~DX12Buffer() {
    if (m_mappedData) {
        D3D12_RANGE range = { 0, 0 };
        m_resource->Unmap(0, &range);
        m_mappedData = nullptr;
    }
}

void DX12Buffer::update(uint32_t offset, uint32_t size, const void* data) {
    if (m_mappedData) {
        // Upload heap: 直接 memcpy
        memcpy(static_cast<uint8_t*>(m_mappedData) + offset, data, size);
    } else {
        // Default heap: 缓存数据，等 UploadContext 上传
        if (offset == 0 && size == m_desc.size) {
            m_pendingData.assign(
                static_cast<const uint8_t*>(data),
                static_cast<const uint8_t*>(data) + size);
        } else {
            if (m_pendingData.size() < m_desc.size) {
                m_pendingData.resize(m_desc.size, 0);
            }
            memcpy(m_pendingData.data() + offset, data, size);
        }
    }
}

bool DX12Buffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (!m_mappedData) return false;
    // Readback heap: 读取映射的数据
    D3D12_RANGE readRange = { offset, offset + size };
    void* mapped = nullptr;
    HRESULT hr = m_resource->Map(0, &readRange, &mapped);
    if (FAILED(hr)) return false;
    memcpy(outData, static_cast<uint8_t*>(mapped) + offset, size);
    D3D12_RANGE writeRange = { 0, 0 };
    m_resource->Unmap(0, &writeRange);
    return true;
}

} // namespace mulan::engine
