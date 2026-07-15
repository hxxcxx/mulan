#include "detail/dx12_buffer.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

namespace mulan::engine {

Result<std::unique_ptr<DX12Buffer>> DX12Buffer::create(const BufferDesc& desc, ID3D12Device* device) {
    if (!device)
        return std::unexpected(makeError(EngineErrorCode::BufferCreateFailed, "DX12Buffer requires a valid device"));

    auto buffer = std::unique_ptr<DX12Buffer>(new DX12Buffer(desc));
    if (auto result = buffer->initialize(device); !result)
        return std::unexpected(makeError(EngineErrorCode::BufferCreateFailed, result.error().message));
    buffer->desc_.discardInitialData();
    return buffer;
}

Result<void> DX12Buffer::initialize(ID3D12Device* device) {
    const auto& desc = desc_;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (desc.bindFlags & BufferBindFlags::UnorderedAccess) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    D3D12_RESOURCE_DESC resDesc = {};

    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = (desc.bindFlags & BufferBindFlags::UniformBuffer)
                            ? ((static_cast<uint64_t>(desc.size) + 255ull) & ~255ull)
                            : desc.size;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = flags;

    switch (desc.usage) {
    case BufferUsage::Immutable:
    case BufferUsage::Default:
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        initialState = D3D12_RESOURCE_STATE_COMMON;
        break;

    case BufferUsage::Dynamic:
        // 上传堆，CPU 可写
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;

    case BufferUsage::Staging:
        // 回读堆，CPU 可读写
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    }

    state_ = initialState;

    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, initialState, nullptr,
                                                 IID_PPV_ARGS(&resource_));
    if (auto result = checkDX12(hr, "ID3D12Device::CreateCommittedResource"); !result)
        return result;

    // Upload 堆保持持久映射。Readback 堆在 readback() 中按 CPU 读取范围映射，
    // 避免对同一个资源重复 Map/Unmap。
    if (desc.usage == BufferUsage::Dynamic) {
        D3D12_RANGE range = { 0, 0 };  // 不读
        if (auto result = checkDX12(resource_->Map(0, &range, &mapped_data_), "ID3D12Resource::Map(upload buffer)");
            !result) {
            return result;
        }
    }

    // 保存初始数据用于后续上传
    if (desc.initData && desc.size > 0) {
        pending_data_.assign(static_cast<const uint8_t*>(desc.initData),
                             static_cast<const uint8_t*>(desc.initData) + desc.size);
    }
    return {};
}

DX12Buffer::~DX12Buffer() {
    waitForLastUseBeforeDestruction();
    if (mapped_data_) {
        D3D12_RANGE range = { 0, 0 };
        resource_->Unmap(0, &range);
        mapped_data_ = nullptr;
    }
}

Result<void> DX12Buffer::write(uint32_t offset, uint32_t size, const void* data) {
    if (auto wait = waitForLastUse(); !wait)
        return std::unexpected(wait.error());
    if (desc_.usage != BufferUsage::Dynamic || !mapped_data_ || !data || size == 0 || offset > desc_.size ||
        size > desc_.size - offset) {
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed,
                                         "DX12 buffer write requires a valid Dynamic buffer range"));
    }
    memcpy(static_cast<uint8_t*>(mapped_data_) + offset, data, size);
    return {};
}

Result<void> DX12Buffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (auto wait = waitForLastUse(); !wait)
        return std::unexpected(wait.error());
    if (desc_.usage != BufferUsage::Staging || !resource_ || !outData || offset > desc_.size ||
        size > desc_.size - offset)
        return std::unexpected(makeError(EngineErrorCode::ResourceReadbackFailed,
                                         "DX12 buffer readback requires a valid staging range"));

    // Readback heap: 仅在 CPU 真正读取时映射，并声明读取范围。
    D3D12_RANGE readRange = { offset, offset + size };
    void* mapped = nullptr;
    HRESULT hr = resource_->Map(0, &readRange, &mapped);
    if (FAILED(hr))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceReadbackFailed, "DX12 staging buffer mapping failed"));
    memcpy(outData, static_cast<uint8_t*>(mapped) + offset, size);
    D3D12_RANGE writeRange = { 0, 0 };
    resource_->Unmap(0, &writeRange);
    return {};
}

}  // namespace mulan::engine
