#include "dx12_upload_context.h"
#include "dx12_buffer.h"
#include "dx12_texture.h"
#include "dx12_convert.h"

namespace mulan::engine {

DX12UploadContext::DX12UploadContext(ID3D12Device* device,
                                     ID3D12CommandQueue* queue,
                                     uint32_t frameCount)
    : device_(device)
    , queue_(queue)
{
    HRESULT hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmd_allocator_));
    DX12_CHECK(hr);

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   cmd_allocator_.Get(), nullptr,
                                   IID_PPV_ARGS(&cmd_list_));
    DX12_CHECK(hr);
    // 创建时处于 open 状态，先关闭
    cmd_list_->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    DX12_CHECK(hr);
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
    if (fence_event_) CloseHandle(fence_event_);
}

DX12UploadContext::StagingSlab& DX12UploadContext::getOrCreateSlab(uint32_t minSize) {
    // 在已有 slab 中找空间
    for (auto& slab : slabs_) {
        if (slab.capacity - slab.used >= minSize) return slab;
    }

    // 创建新 slab
    uint32_t slabSize = (minSize > kStagingSize) ? minSize : kStagingSize;

    StagingSlab slab;
    slab.capacity = slabSize;
    slab.used = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment        = 0;
    desc.Width            = slabSize;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&slab.resource));
    DX12_CHECK(hr);

    D3D12_RANGE range = { 0, 0 };
    slab.resource->Map(0, &range, &slab.mapped);

    slabs_.push_back(std::move(slab));
    return slabs_.back();
}

void DX12UploadContext::uploadBuffer(DX12Buffer* dst, const void* data,
                                      uint32_t size, uint32_t dstOffset) {
    auto& slab = getOrCreateSlab(size);
    uint32_t offset = slab.used;

    // 拷贝到 staging
    memcpy(static_cast<uint8_t*>(slab.mapped) + offset, data, size);
    slab.used += size;
    // 对齐到 256 字节（D3D12 要求）
    slab.used = (slab.used + 255u) & ~255u;

    // 录制 CopyTextureRegion / CopyBufferRegion
    cmd_allocator_->Reset();
    cmd_list_->Reset(cmd_allocator_.Get(), nullptr);

    // 确保目标处于 COPY_DEST 状态
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = dst->resource();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd_list_->ResourceBarrier(1, &barrier);

    cmd_list_->CopyBufferRegion(dst->resource(), dstOffset,
                                slab.resource.Get(), offset, size);

    // 转回 COMMON
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    cmd_list_->ResourceBarrier(1, &barrier);

    cmd_list_->Close();

    ID3D12CommandList* lists[] = { cmd_list_.Get() };
    queue_->ExecuteCommandLists(1, lists);

    fence_value_++;
    queue_->Signal(fence_.Get(), fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);

    dst->markUploaded();
}

void DX12UploadContext::uploadTexture(DX12Texture* dst, const void* data,
                                      uint32_t width, uint32_t height,
                                      TextureFormat format) {
    const uint32_t bpp = textureFormatBytesPerPixel(format);
    if (bpp == 0 || width == 0 || height == 0) return;

    // 用 GetCopyableFootprints 计算对齐后的 row pitch 与总 staging 大小
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment        = 0;
    texDesc.Width            = width;
    texDesc.Height           = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = toDXGIFormat(format);
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalSize = 0;
    device_->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows,
                                   &rowSizeInBytes, &totalSize);

    // 分配 staging（按 256 对齐 slab.used）
    auto& slab = getOrCreateSlab(static_cast<uint32_t>(totalSize));
    const uint32_t slabOffset = slab.used;

    // 逐行拷贝：源行距 = width*bpp，目标行距 = footprint.Footprint.RowPitch
    const auto* src = static_cast<const uint8_t*>(data);
    auto* dstPtr = static_cast<uint8_t*>(slab.mapped) + slabOffset;
    const uint32_t srcRowSize = width * bpp;
    for (UINT r = 0; r < numRows; ++r) {
        memcpy(dstPtr + r * footprint.Footprint.RowPitch,
               src + r * srcRowSize, srcRowSize);
    }
    slab.used += static_cast<uint32_t>(totalSize);
    slab.used = (slab.used + 255u) & ~255u;

    // 录制
    cmd_allocator_->Reset();
    cmd_list_->Reset(cmd_allocator_.Get(), nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = dst->resource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = dst->state();
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd_list_->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource        = slab.resource.Get();
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint  = footprint;
    srcLoc.PlacedFootprint.Offset = slabOffset;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource        = dst->resource();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    cmd_list_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // COPY_DEST → PIXEL_SHADER_RESOURCE
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd_list_->ResourceBarrier(1, &barrier);

    cmd_list_->Close();

    ID3D12CommandList* lists[] = { cmd_list_.Get() };
    queue_->ExecuteCommandLists(1, lists);

    fence_value_++;
    queue_->Signal(fence_.Get(), fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);

    dst->setState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void DX12UploadContext::flush() {
    // 当前实现是同步上传，flush 无需额外操作
}

} // namespace mulan::engine
