/**
 * @file DX12UploadContext.cpp
 * @brief D3D12 上传管理器实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12UploadContext.h"
#include "DX12Buffer.h"

namespace mulan::engine {

DX12UploadContext::DX12UploadContext(ID3D12Device* device,
                                     ID3D12CommandQueue* queue,
                                     uint32_t frameCount)
    : m_device(device)
    , m_queue(queue)
{
    HRESULT hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_cmdAllocator));
    DX12_CHECK(hr);

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   m_cmdAllocator.Get(), nullptr,
                                   IID_PPV_ARGS(&m_cmdList));
    DX12_CHECK(hr);
    // 创建时处于 open 状态，先关闭
    m_cmdList->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    DX12_CHECK(hr);
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

DX12UploadContext::~DX12UploadContext() {
    flush();
    for (auto& slab : m_slabs) {
        if (slab.mapped) {
            D3D12_RANGE range = { 0, 0 };
            slab.resource->Unmap(0, &range);
        }
    }
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

DX12UploadContext::StagingSlab& DX12UploadContext::getOrCreateSlab(uint32_t minSize) {
    // 在已有 slab 中找空间
    for (auto& slab : m_slabs) {
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

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&slab.resource));
    DX12_CHECK(hr);

    D3D12_RANGE range = { 0, 0 };
    slab.resource->Map(0, &range, &slab.mapped);

    m_slabs.push_back(std::move(slab));
    return m_slabs.back();
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
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

    // 确保目标处于 COPY_DEST 状态
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = dst->resource();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    m_cmdList->ResourceBarrier(1, &barrier);

    m_cmdList->CopyBufferRegion(dst->resource(), dstOffset,
                                slab.resource.Get(), offset, size);

    // 转回 COMMON
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
    m_cmdList->ResourceBarrier(1, &barrier);

    m_cmdList->Close();

    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    m_fenceValue++;
    m_queue->Signal(m_fence.Get(), m_fenceValue);
    m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);

    dst->markUploaded();
}

void DX12UploadContext::flush() {
    // 当前实现是同步上传，flush 无需额外操作
}

} // namespace mulan::engine
