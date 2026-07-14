#include "detail/dx12_upload_context.h"
#include "detail/dx12_buffer.h"
#include "detail/dx12_texture.h"
#include "detail/dx12_convert.h"
#include "../rhi/engine_error_code.h"

namespace mulan::engine {

DX12UploadContext::DX12UploadContext(ID3D12Device* device, ID3D12CommandQueue* queue, uint32_t frameCount)
    : device_(device), queue_(queue) {
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_allocator_));
    if (!checkDX12(hr, "ID3D12Device::CreateCommandAllocator"))
        return;

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocator_.Get(), nullptr,
                                   IID_PPV_ARGS(&cmd_list_));
    if (!checkDX12(hr, "ID3D12Device::CreateCommandList"))
        return;
    // 创建时处于 open 状态，先关闭
    cmd_list_->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (!checkDX12(hr, "ID3D12Device::CreateFence"))
        return;
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

DX12UploadContext::~DX12UploadContext() {
    flush();
    for (auto& slab : slabs_) {
        if (slab.mapped) {
            D3D12_RANGE range = { 0, 0 };
            slab.resource->Unmap(0, &range);
        }
    }
    if (fence_event_)
        CloseHandle(fence_event_);
}

DX12UploadContext::StagingSlab* DX12UploadContext::getOrCreateSlab(uint64_t minSize, uint32_t alignment,
                                                                   uint32_t& alignedOffset) {
    if (minSize > UINT32_MAX) {
        LOG_ERROR("[DX12] Staging allocation rejected: size exceeds 4 GiB");
        return nullptr;
    }
    // 在已有 slab 中找空间
    for (auto& slab : slabs_) {
        const uint64_t offset = (static_cast<uint64_t>(slab.used) + alignment - 1) & ~(alignment - 1ull);
        if (offset + minSize <= slab.capacity) {
            alignedOffset = static_cast<uint32_t>(offset);
            return &slab;
        }
    }

    // 创建新 slab
    uint32_t slabSize = static_cast<uint32_t>(std::max<uint64_t>(minSize, kStagingSize));

    StagingSlab slab;
    slab.capacity = slabSize;
    slab.used = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = slabSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr =
            device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, IID_PPV_ARGS(&slab.resource));
    if (!checkDX12(hr, "ID3D12Device::CreateCommittedResource(upload slab)"))
        return nullptr;

    D3D12_RANGE range = { 0, 0 };
    if (!checkDX12(slab.resource->Map(0, &range, &slab.mapped), "ID3D12Resource::Map(upload slab)"))
        return nullptr;

    slabs_.push_back(std::move(slab));
    alignedOffset = 0;
    return &slabs_.back();
}

core::Result<void> DX12UploadContext::uploadBuffer(DX12Buffer* dst, const void* data, uint32_t size,
                                                   uint32_t dstOffset) {
    if (!dst || !dst->resource() || !data || size == 0 || static_cast<uint64_t>(dstOffset) + size > dst->desc().size)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 buffer upload arguments are invalid"));
    uint32_t offset = 0;
    auto* slab = getOrCreateSlab(size, 256, offset);
    if (!slab)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 buffer staging allocation failed"));

    // 拷贝到 staging
    memcpy(static_cast<uint8_t*>(slab->mapped) + offset, data, size);
    slab->used = offset + size;

    // 非批量模式：每次提交都需 Reset 重新开录；批量模式：cmd_list 已 open，直接 Record
    if (!batch_active_) {
        if (!checkDX12(cmd_allocator_->Reset(), "ID3D12CommandAllocator::Reset(buffer upload)"))
            return std::unexpected(
                    makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload allocator reset failed"));
        if (!checkDX12(cmd_list_->Reset(cmd_allocator_.Get(), nullptr),
                       "ID3D12GraphicsCommandList::Reset(buffer upload)"))
            return std::unexpected(
                    makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload command-list reset failed"));
    }

    // 确保目标处于 COPY_DEST 状态
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dst->resource();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd_list_->ResourceBarrier(1, &barrier);

    cmd_list_->CopyBufferRegion(dst->resource(), dstOffset, slab->resource.Get(), offset, size);

    // 转回 COMMON
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    cmd_list_->ResourceBarrier(1, &barrier);

    if (auto result = submitIfNotBatching(); !result)
        return std::unexpected(result.error());
    dst->markUploaded();
    return {};
}

core::Result<void> DX12UploadContext::uploadTexture(DX12Texture* dst, const TextureUploadDesc& upload) {
    const uint32_t bpp = textureFormatBytesPerPixel(upload.format);
    const uint32_t sourceRowPitch = upload.sourceRowPitch ? upload.sourceRowPitch : upload.width * bpp;
    if (!dst || upload.data.empty() || bpp == 0 || upload.width == 0 || upload.height == 0 ||
        sourceRowPitch < upload.width * bpp ||
        upload.data.size_bytes() < static_cast<size_t>(sourceRowPitch) * upload.height)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 texture upload arguments are invalid"));

    const auto texDesc = dst->resource()->GetDesc();
    const uint32_t subresource = upload.mipLevel + upload.arrayLayer * dst->desc().mipLevels;
    if (upload.mipLevel >= dst->desc().mipLevels || upload.arrayLayer >= dst->desc().arraySize ||
        upload.format != dst->desc().format || upload.width != std::max(1u, dst->desc().width >> upload.mipLevel) ||
        upload.height != std::max(1u, dst->desc().height >> upload.mipLevel))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 texture upload subresource is invalid"));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalSize = 0;
    device_->GetCopyableFootprints(&texDesc, subresource, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalSize);

    // 分配 staging（按 256 对齐 slab.used）
    uint32_t slabOffset = 0;
    auto* slab = getOrCreateSlab(totalSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, slabOffset);
    if (!slab)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 texture staging allocation failed"));

    // 逐行拷贝：源行距 = width*bpp，目标行距 = footprint.Footprint.RowPitch
    const auto* src = reinterpret_cast<const uint8_t*>(upload.data.data());
    auto* dstPtr = static_cast<uint8_t*>(slab->mapped) + slabOffset;
    const uint32_t srcRowSize = upload.width * bpp;
    for (UINT r = 0; r < numRows; ++r) {
        memcpy(dstPtr + r * footprint.Footprint.RowPitch, src + r * sourceRowPitch, srcRowSize);
    }
    slab->used = slabOffset + static_cast<uint32_t>(totalSize);

    // 录制
    if (!batch_active_) {
        if (!checkDX12(cmd_allocator_->Reset(), "ID3D12CommandAllocator::Reset(upload)"))
            return std::unexpected(
                    makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload allocator reset failed"));
        if (!checkDX12(cmd_list_->Reset(cmd_allocator_.Get(), nullptr), "ID3D12GraphicsCommandList::Reset(upload)"))
            return std::unexpected(
                    makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload command-list reset failed"));
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dst->resource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = dst->state();
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd_list_->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = slab->resource.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;
    srcLoc.PlacedFootprint.Offset = slabOffset;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = dst->resource();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = subresource;

    cmd_list_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // COPY_DEST → PIXEL_SHADER_RESOURCE
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd_list_->ResourceBarrier(1, &barrier);

    if (auto result = submitIfNotBatching(); !result)
        return std::unexpected(result.error());
    dst->setState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    return {};
}

core::Result<void> DX12UploadContext::beginUploadBatch() {
    if (batch_active_)
        return {};
    if (!checkDX12(cmd_allocator_->Reset(), "ID3D12CommandAllocator::Reset(upload batch)"))
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload allocator reset failed"));
    if (!checkDX12(cmd_list_->Reset(cmd_allocator_.Get(), nullptr), "ID3D12GraphicsCommandList::Reset(upload batch)"))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload command-list reset failed"));
    batch_active_ = true;
    return {};
}

core::Result<void> DX12UploadContext::flushUploadBatch() {
    if (!batch_active_)
        return {};
    batch_active_ = false;
    if (!checkDX12(cmd_list_->Close(), "ID3D12GraphicsCommandList::Close(upload batch)"))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload command-list close failed"));

    ID3D12CommandList* lists[] = { cmd_list_.Get() };
    queue_->ExecuteCommandLists(1, lists);

    fence_value_++;
    if (!checkDX12(queue_->Signal(fence_.Get(), fence_value_), "ID3D12CommandQueue::Signal(upload batch)"))
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload signal failed"));
    if (!checkDX12(fence_->SetEventOnCompletion(fence_value_, fence_event_),
                   "ID3D12Fence::SetEventOnCompletion(upload batch)"))
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload wait setup failed"));
    if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0)
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload wait failed"));

    // 本批次 GPU 已完成，staging slab 空间可回收复用
    for (auto& slab : slabs_)
        slab.used = 0;
    return {};
}

core::Result<void> DX12UploadContext::submitIfNotBatching() {
    if (batch_active_)
        return {};

    if (!checkDX12(cmd_list_->Close(), "ID3D12GraphicsCommandList::Close(upload)"))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload command-list close failed"));

    ID3D12CommandList* lists[] = { cmd_list_.Get() };
    queue_->ExecuteCommandLists(1, lists);

    fence_value_++;
    if (!checkDX12(queue_->Signal(fence_.Get(), fence_value_), "ID3D12CommandQueue::Signal(upload)"))
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload signal failed"));
    if (!checkDX12(fence_->SetEventOnCompletion(fence_value_, fence_event_),
                   "ID3D12Fence::SetEventOnCompletion(upload)"))
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload wait setup failed"));
    if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0)
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "DX12 upload wait failed"));

    // 同步提交完成后，staging 空间可复用
    for (auto& slab : slabs_)
        slab.used = 0;
    return {};
}

void DX12UploadContext::flush() {
    // 当前实现是同步上传，flush 无需额外操作
}

}  // namespace mulan::engine
