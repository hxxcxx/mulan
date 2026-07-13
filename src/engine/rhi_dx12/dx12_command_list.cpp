#include "detail/dx12_command_list.h"
#include "detail/dx12_buffer.h"
#include "detail/dx12_texture.h"
#include "detail/dx12_pipeline_state.h"
#include "detail/dx12_convert.h"
#include "detail/dx12_bind_group.h"
#include "detail/dx12_sampler.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

core::Result<std::unique_ptr<DX12CommandList>> DX12CommandList::create(ID3D12Device* device,
                                                                       ID3D12CommandAllocator* allocator) {
    if (!device || !allocator)
        return std::unexpected(makeError(EngineErrorCode::CommandListCreateFailed, "Invalid command list arguments"));
    auto object = std::unique_ptr<DX12CommandList>(new DX12CommandList(device, allocator));
    if (!object->cmd_list_)
        return std::unexpected(makeError(EngineErrorCode::CommandListCreateFailed, "CreateCommandList failed"));
    return object;
}

DX12CommandList::DX12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator)
    : allocator_(allocator), owns_cmd_list_(true) {
    HRESULT hr =
            device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&cmd_list_));
    if (!checkDX12(hr, "ID3D12Device::CreateCommandList"))
        return;
    cmd_list_->Close();
}

DX12CommandList::DX12CommandList(ID3D12GraphicsCommandList* existingCmdList) : owns_cmd_list_(false) {
    if (existingCmdList) {
        cmd_list_ = existingCmdList;
    }
    // nullptr 是合法的 — 用于创建占位 wrapper，后续通过 setCommandList 设置
}

DX12CommandList::~DX12CommandList() {
    if (!owns_cmd_list_) {
        cmd_list_.Detach();  // 不 Release 外部的 cmd list
    }
}

void DX12CommandList::setCommandList(ID3D12GraphicsCommandList* cmdList) {
    if (!owns_cmd_list_) {
        cmd_list_.Detach();
    }
    cmd_list_ = cmdList;
    owns_cmd_list_ = false;
    recording_ = cmdList != nullptr;
}

void DX12CommandList::begin() {
    resetResourceUsage();
    if (!cmd_list_)
        return;

    if (owns_cmd_list_) {
        // 独立命令列表由 create() 先关闭；begin() 负责复用 allocator 并重新打开。
        if (recording_ || !allocator_)
            return;
        if (!checkDX12(allocator_->Reset(), "ID3D12CommandAllocator::Reset"))
            return;
        if (!checkDX12(cmd_list_->Reset(allocator_.Get(), nullptr), "ID3D12GraphicsCommandList::Reset"))
            return;
    }

    // 帧循环模式的 allocator/list 已由 DX12FrameContext reset；这里仅标记录制状态。
    recording_ = true;
    bindDescriptorHeaps();
}

void DX12CommandList::end() {
    if (!cmd_list_ || !recording_)
        return;
    HRESULT hr = cmd_list_->Close();
    if (!checkDX12(hr, "ID3D12GraphicsCommandList::Close"))
        return;
    recording_ = false;
}

void DX12CommandList::setPipelineState(PipelineState* pso) {
    recordResourceUse(pso);
    auto* dx12Pso = static_cast<DX12PipelineState*>(pso);
    cmd_list_->SetPipelineState(dx12Pso->pipeline());
    cmd_list_->SetGraphicsRootSignature(dx12Pso->rootSignature());
    cmd_list_->IASetPrimitiveTopology(toDX12Topology(pso->desc().topology));
    cached_stride_ = pso->desc().vertexLayout.stride();
}

void DX12CommandList::setViewport(const Viewport& vp) {
    D3D12_VIEWPORT d3dVp = { vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
    cmd_list_->RSSetViewports(1, &d3dVp);
}

void DX12CommandList::setScissorRect(const ScissorRect& rect) {
    D3D12_RECT d3dRect = { rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
    cmd_list_->RSSetScissorRects(1, &d3dRect);
}

void DX12CommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    recordResourceUse(buffer);
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = dx12Buf->gpuAddress() + offset;
    vbv.SizeInBytes = buffer->size() - offset;
    vbv.StrideInBytes = cached_stride_;
    cmd_list_->IASetVertexBuffers(slot, 1, &vbv);
}

void DX12CommandList::setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
    if (buffers) {
        for (uint32_t i = 0; i < count; ++i)
            recordResourceUse(buffers[i]);
    }
    D3D12_VERTEX_BUFFER_VIEW vbvs[16] = {};
    for (uint32_t i = 0; i < count && i < 16; ++i) {
        auto* dx12Buf = static_cast<DX12Buffer*>(buffers[i]);
        vbvs[i].BufferLocation = dx12Buf->gpuAddress() + (offsets ? offsets[i] : 0);
        vbvs[i].SizeInBytes = buffers[i]->size() - (offsets ? offsets[i] : 0);
        vbvs[i].StrideInBytes = cached_stride_;
    }
    cmd_list_->IASetVertexBuffers(startSlot, count, vbvs);
}

void DX12CommandList::setIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    recordResourceUse(buffer);
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = dx12Buf->gpuAddress() + offset;
    ibv.SizeInBytes = buffer->size() - offset;
    ibv.Format = (type == IndexType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    cmd_list_->IASetIndexBuffer(&ibv);
}

void DX12CommandList::draw(const DrawAttribs& attribs) {
    cmd_list_->DrawInstanced(attribs.vertexCount, attribs.instanceCount, attribs.startVertex, attribs.startInstance);
}

void DX12CommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    cmd_list_->DrawIndexedInstanced(attribs.indexCount, attribs.instanceCount, attribs.startIndex, attribs.baseVertex,
                                    attribs.startInstance);
}

void DX12CommandList::drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t /*stride*/) {
    recordResourceUse(argsBuffer);
    if (!draw_indirect_sig_)
        return;
    auto* dx12Buf = static_cast<DX12Buffer*>(argsBuffer);
    cmd_list_->ExecuteIndirect(draw_indirect_sig_, drawCount, dx12Buf->resource(), offset, nullptr, 0);
}

void DX12CommandList::dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
    cmd_list_->Dispatch(threadGroupX, threadGroupY, threadGroupZ);
}

void DX12CommandList::dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    recordResourceUse(argsBuffer);
    if (!dispatch_indirect_sig_)
        return;
    auto* dx12Buf = static_cast<DX12Buffer*>(argsBuffer);
    cmd_list_->ExecuteIndirect(dispatch_indirect_sig_, 1, dx12Buf->resource(), offset, nullptr, 0);
}

void DX12CommandList::setPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t /*stageFlags*/) {
    // DX12 root constants: binding slot 3 reserved for push constants
    // (slot 0=scene UBO, 1=object UBO, 2=material UBO, 3=push constants)
    uint32_t count = size / 4;
    if (count > 0) {
        cmd_list_->SetGraphicsRoot32BitConstants(3, count, data, offset / 4);
    }
}

void DX12CommandList::updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                                   ResourceTransitionMode mode) {
    recordResourceUse(buffer);
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    dx12Buf->update(offset, size, data);
}

void DX12CommandList::bindGroup(BindGroup& group) {
    recordBindGroupUse(group);
    auto* dx12Group = static_cast<DX12BindGroup*>(&group);
    if (dx12Group->entryCount() == 0 || (!desc_heap_ && !sampler_heap_))
        return;

    // --- 跨帧失效：per-frame heap reset 已回收上一帧的 descriptor 区段，
    // 若 BindGroup 缓存的 descriptor handles 不属于当前帧则丢弃并强制本帧完整重写。
    if (dx12Group->frameToken() != frame_token_) {
        dx12Group->clearCachedTextureHandles();
        dx12Group->setFrameToken(frame_token_);
        dx12Group->markAllDirty();
    }

    bool hasTexture = false;
    for (uint8_t i = 0; i < dx12Group->entryCount(); ++i) {
        if (dx12Group->entries()[i].type == DescriptorType::TextureSRV && dx12Group->entries()[i].texture) {
            hasTexture = true;
            break;
        }
    }

    if (hasTexture && !desc_heap_) {
        LOG_ERROR("[DX12] bindGroup rejected: CBV/SRV/UAV heap is not bound");
        return;
    }

    ID3D12Device* device = nullptr;
    cmd_list_->GetDevice(IID_PPV_ARGS(&device));

    // 遍历 entries，按类型绑定：
    //   UBO     → root CBV（直接 GPU 地址，无需 descriptor heap）
    //   Texture → 当前 binding 的不可变 descriptor snapshot
    //   Sampler → sampler heap 中的持久 descriptor table
    //
    // 不能把纹理 descriptor 原地覆盖：同一 command list 中较早的 draw 可能尚未
    // 执行，覆盖后它也会看到后一个 draw 的纹理。纹理发生变化时为该 binding
    // 分配新 slot；未变化时复用旧 slot。
    const uint16_t mask = dx12Group->dirtyMask();
    uint16_t written = 0;

    for (uint8_t i = 0; i < dx12Group->entryCount(); ++i) {
        const auto& e = dx12Group->entries()[i];
        uint32_t rootIdx = dx12Group->rootIndexForBinding(e.binding);
        if (rootIdx == DX12BindGroup::kInvalidRootIndex)
            continue;

        if (e.type == DescriptorType::UniformBuffer && e.buffer) {
            // UBO：root CBV 每 draw 必须重设（offset 变化），不依赖脏位
            auto* dx12Buf = static_cast<DX12Buffer*>(e.buffer);
            cmd_list_->SetGraphicsRootConstantBufferView(rootIdx, dx12Buf->gpuAddress() + e.offset);
        } else if (e.type == DescriptorType::TextureSRV && e.texture) {
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12Group->cachedTextureHandle(i);
            const bool descriptorDirty = ((mask >> i) & 1u) != 0;
            if (!gpuHandle.ptr || descriptorDirty) {
                auto* dx12Tex = static_cast<DX12Texture*>(e.texture);
                if (!dx12Tex || !dx12Tex->srv().ptr || !device) {
                    LOG_ERROR("[DX12] bindGroup rejected: texture has no valid SRV");
                    continue;
                }
                if (desc_alloc_count_ >= desc_capacity_) {
                    LOG_ERROR("[DX12] bindGroup rejected: shader-visible descriptor heap exhausted");
                    continue;
                }

                gpuHandle.ptr = desc_gpu_base_.ptr + desc_alloc_count_ * desc_size_;
                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
                cpuHandle.ptr = desc_cpu_base_.ptr + desc_alloc_count_ * desc_size_;
                device->CopyDescriptorsSimple(1, cpuHandle, dx12Tex->srv(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                ++desc_alloc_count_;
                dx12Group->setCachedTextureHandle(i, gpuHandle);
                written |= (uint16_t(1) << i);
            }

            if (gpuHandle.ptr)
                cmd_list_->SetGraphicsRootDescriptorTable(rootIdx, gpuHandle);
        } else if (e.type == DescriptorType::Sampler && e.sampler) {
            auto* dx12Sampler = static_cast<DX12Sampler*>(e.sampler);
            const D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle = dx12Sampler->gpuHandle();
            if (!samplerHandle.ptr || !sampler_heap_) {
                LOG_ERROR("[DX12] bindGroup rejected: sampler descriptor is not shader-visible");
                continue;
            }

            // Sampler descriptors are allocated from a persistent shader-visible
            // heap at sampler creation time, so no per-frame copy is required.
            cmd_list_->SetGraphicsRootDescriptorTable(rootIdx, samplerHandle);
            written |= (uint16_t(1) << i);
        }
    }

    if (device)
        device->Release();
    dx12Group->clearDirty(written);
}

void DX12CommandList::bindResources(const BindGroupDesc& desc) {
    recordBindGroupUse(desc);
    // 注意：此便捷路径仅接收 BindGroupDesc（无 BindGroupLayout），无法得知各 binding
    // 的 DescriptorType，因此无法做 binding→root-parameter-index 映射。当前引擎渲染
    // 路径一律走 bindGroup(BindGroup&)（后者有完整映射）。该便捷路径也不处理 sampler；
    // 如需绑定 sampler，必须改为接收 layout 或 PSO 以正确计算 root index。
    for (uint8_t i = 0; i < desc.count; ++i) {
        const auto& e = desc.entries[i];
        if (e.type == DescriptorType::UniformBuffer && e.buffer) {
            auto* dx12Buf = static_cast<DX12Buffer*>(e.buffer);
            cmd_list_->SetGraphicsRootConstantBufferView(e.binding, dx12Buf->gpuAddress() + e.offset);
        } else if (e.type == DescriptorType::TextureSRV && e.texture && desc_heap_) {
            auto* dx12Tex = static_cast<DX12Texture*>(e.texture);
            if (!dx12Tex->srv().ptr)
                continue;
            if (desc_alloc_count_ >= desc_capacity_) {
                LOG_ERROR("[DX12] bindResources rejected: shader-visible descriptor heap exhausted");
                continue;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = desc_cpu_base_;
            cpuHandle.ptr += desc_alloc_count_ * desc_size_;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = desc_gpu_base_;
            gpuHandle.ptr += desc_alloc_count_ * desc_size_;
            ++desc_alloc_count_;

            ID3D12Device* device = nullptr;
            cmd_list_->GetDevice(IID_PPV_ARGS(&device));
            if (device) {
                device->CopyDescriptorsSimple(1, cpuHandle, dx12Tex->srv(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                device->Release();
            }
            cmd_list_->SetGraphicsRootDescriptorTable(e.binding, gpuHandle);
        }
    }
}

void DX12CommandList::setDescriptorHeap(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE cpuBase,
                                        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase, uint32_t descriptorSize,
                                        ID3D12DescriptorHeap* samplerHeap, uint32_t descriptorCapacity) {
    desc_heap_ = heap;
    sampler_heap_ = samplerHeap;
    desc_cpu_base_ = cpuBase;
    desc_gpu_base_ = gpuBase;
    desc_size_ = descriptorSize;
    desc_capacity_ = descriptorCapacity > 0 ? descriptorCapacity : 1024;
    desc_alloc_count_ = 0;

    // 把 shader-visible descriptor heap 绑定到命令列表。
    // D3D12 要求：使用 root descriptor table 引用 GPU handle 前，必须先
    // SetDescriptorHeaps，否则 descriptor table 引用的 GPU handle 无效。
    // 每帧 beginFrame 会 reset heap 后重新调用此函数，此处随之重新绑定。
    bindDescriptorHeaps();
}

void DX12CommandList::bindDescriptorHeaps() {
    if (!cmd_list_ || !recording_)
        return;

    ID3D12DescriptorHeap* heaps[2] = {};
    UINT heapCount = 0;
    if (desc_heap_)
        heaps[heapCount++] = desc_heap_;
    if (sampler_heap_)
        heaps[heapCount++] = sampler_heap_;
    if (heapCount > 0)
        cmd_list_->SetDescriptorHeaps(heapCount, heaps);
}

void DX12CommandList::transitionResource(Buffer* buffer, ResourceState newState) {
    recordResourceUse(buffer);
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    if (!dx12Buf || !dx12Buf->resource())
        return;

    const D3D12_RESOURCE_STATES before = dx12Buf->state();
    const D3D12_RESOURCE_STATES after = toDX12ResourceStates(newState);
    if (before == after)
        return;

    // Upload/readback heaps have restricted legal states. In particular, a
    // readback buffer must remain COPY_DEST while the GPU writes into it.
    if (dx12Buf->usage() == BufferUsage::Staging || dx12Buf->usage() == BufferUsage::Dynamic) {
        LOG_ERROR("[DX12] transitionResource rejected: buffer heap does not support requested state transition");
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dx12Buf->resource();
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &barrier);
    dx12Buf->setState(after);
}

void DX12CommandList::transitionResource(Texture* texture, ResourceState newState) {
    recordResourceUse(texture);
    auto* dx12Tex = static_cast<DX12Texture*>(texture);
    if (!dx12Tex || !dx12Tex->resource())
        return;

    const D3D12_RESOURCE_STATES before = dx12Tex->state();
    const D3D12_RESOURCE_STATES after = toDX12ResourceStates(newState);
    if (before == after)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dx12Tex->resource();
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &barrier);
    dx12Tex->setState(after);
}

bool DX12CommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    recordResourceUse(src);
    recordResourceUse(dst);
    auto* dx12Tex = static_cast<DX12Texture*>(src);
    auto* dx12Buf = static_cast<DX12Buffer*>(dst);
    if (!dx12Tex || !dx12Buf || !dx12Tex->resource() || !dx12Buf->resource())
        return false;

    // 取 device 用于 GetCopyableFootprints（与 bindResources 同模式）
    ID3D12Device* device = nullptr;
    cmd_list_->GetDevice(IID_PPV_ARGS(&device));
    if (!device)
        return false;

    // 构建与纹理一致的 resource desc，用于计算可拷贝的 footprint
    const auto& td = dx12Tex->desc();
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = td.width;
    texDesc.Height = td.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = toDXGIFormat(td.format);
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalSize);
    device->Release();

    if (dx12Buf->size() < totalSize) {
        LOG_ERROR("[DX12] copyTextureToBuffer rejected: destination buffer is too small for texture footprint");
        return false;
    }

    const D3D12_RESOURCE_STATES originalTexState = dx12Tex->state();
    const D3D12_RESOURCE_STATES originalBufState = dx12Buf->state();

    if (originalBufState != D3D12_RESOURCE_STATE_COPY_DEST &&
        (dx12Buf->usage() == BufferUsage::Staging || dx12Buf->usage() == BufferUsage::Dynamic)) {
        LOG_ERROR("[DX12] copyTextureToBuffer rejected: destination buffer is not in COPY_DEST state");
        return false;
    }

    // Transition the source only when necessary. The caller may already have
    // transitioned it through CommandList::transitionResource().
    D3D12_RESOURCE_BARRIER texBarrier = {};
    texBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    texBarrier.Transition.pResource = dx12Tex->resource();
    texBarrier.Transition.StateBefore = originalTexState;
    texBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    texBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (originalTexState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        cmd_list_->ResourceBarrier(1, &texBarrier);
        dx12Tex->setState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    // Readback heaps are created directly in COPY_DEST and must remain there;
    // do not fabricate a COMMON -> COPY_DEST -> COMMON sequence for them.
    D3D12_RESOURCE_BARRIER bufBarrier = {};
    bool bufferTransitioned = false;
    if (originalBufState != D3D12_RESOURCE_STATE_COPY_DEST) {
        bufBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bufBarrier.Transition.pResource = dx12Buf->resource();
        bufBarrier.Transition.StateBefore = originalBufState;
        bufBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        bufBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd_list_->ResourceBarrier(1, &bufBarrier);
        dx12Buf->setState(D3D12_RESOURCE_STATE_COPY_DEST);
        bufferTransitioned = true;
    }

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.pResource = dx12Tex->resource();
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.pResource = dx12Buf->resource();
    dstLoc.PlacedFootprint = footprint;

    cmd_list_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    if (bufferTransitioned) {
        bufBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        bufBarrier.Transition.StateAfter = originalBufState;
        cmd_list_->ResourceBarrier(1, &bufBarrier);
        dx12Buf->setState(originalBufState);
    }

    if (originalTexState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        texBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        texBarrier.Transition.StateAfter = originalTexState;
        cmd_list_->ResourceBarrier(1, &texBarrier);
        dx12Tex->setState(originalTexState);
    }
    return true;
}

void DX12CommandList::clearColor(float r, float g, float b, float a) {
    // 由 SwapChain/RenderTarget 在 beginRenderPass 中处理
}

void DX12CommandList::clearDepth(float depth) {
    // 由 SwapChain/RenderTarget 在 beginRenderPass 中处理
}

void DX12CommandList::clearStencil(uint8_t stencil) {
    // 由 beginRenderPass 中处理
}

// ============================================================
// RenderPass
// ============================================================

void DX12CommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    recordRenderPassUse(info);
    auto* cl = cmd_list_.Get();
    rp_present_source_ = info.presentSource;

    // Color attachment barrier: current → RENDER_TARGET
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<DX12Texture*>(info.colorAttachments[i].target);
        if (!tex)
            continue;

        D3D12_RESOURCE_STATES before = tex->state();
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (tex->resource() && before != after) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = tex->resource();
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
            tex->setState(after);
        }

        // Clear
        if (info.colorAttachments[i].loadAction == LoadAction::Clear) {
            cl->ClearRenderTargetView(tex->rtv(), info.clearColor, 0, nullptr);
        }

        if (info.colorAttachments[i].resolveTarget) {
            auto* resolveTex = static_cast<DX12Texture*>(info.colorAttachments[i].resolveTarget);
            D3D12_RESOURCE_STATES resolveBefore = resolveTex->state();
            D3D12_RESOURCE_STATES resolveAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
            if (resolveTex->resource() && resolveBefore != resolveAfter) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = resolveTex->resource();
                barrier.Transition.StateBefore = resolveBefore;
                barrier.Transition.StateAfter = resolveAfter;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cl->ResourceBarrier(1, &barrier);
                resolveTex->setState(resolveAfter);
            }
        }
    }

    // Depth attachment
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = nullptr;
    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<DX12Texture*>(info.depthAttachment.target);

        D3D12_RESOURCE_STATES before = depthTex->state();
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if (depthTex->resource() && before != after) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = depthTex->resource();
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
            depthTex->setState(after);
        }

        if (info.depthAttachment.loadAction == LoadAction::Clear) {
            cl->ClearDepthStencilView(depthTex->dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                      info.clearDepth, info.clearStencil, 0, nullptr);
        }
        dsvHandle = depthTex->dsv();
        pDSV = &dsvHandle;
    }

    // Set render targets (use first color attachment's RTV)
    DX12Texture* colorTex = nullptr;
    if (info.colorCount > 0) {
        colorTex = static_cast<DX12Texture*>(info.colorAttachments[0].target);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = colorTex->rtv();
        cl->OMSetRenderTargets(1, &rtvHandle, FALSE, pDSV);
    }

    rp_color_tex_ = colorTex;
    rp_resolve_tex_ = (info.colorCount > 0 && info.colorAttachments[0].resolveTarget)
                              ? static_cast<DX12Texture*>(info.colorAttachments[0].resolveTarget)
                              : nullptr;
}

void DX12CommandList::endRenderPass() {
    if (!rp_color_tex_)
        return;
    auto* cl = cmd_list_.Get();

    DX12Texture* finalColorTex = rp_resolve_tex_ ? rp_resolve_tex_ : rp_color_tex_;
    if (rp_resolve_tex_) {
        D3D12_RESOURCE_STATES beforeResolve = rp_color_tex_->state();
        if (rp_color_tex_->resource() && beforeResolve != D3D12_RESOURCE_STATE_RESOLVE_SOURCE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = rp_color_tex_->resource();
            barrier.Transition.StateBefore = beforeResolve;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
            rp_color_tex_->setState(D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        }

        cl->ResolveSubresource(rp_resolve_tex_->resource(), 0, rp_color_tex_->resource(), 0,
                               toDXGIFormat(rp_resolve_tex_->format()));
    }

    D3D12_RESOURCE_STATES targetState =
            rp_present_source_ ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_RESOURCE_STATES before = finalColorTex->state();
    D3D12_RESOURCE_STATES after = targetState;
    if (finalColorTex->resource() && before != after) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = finalColorTex->resource();
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(1, &barrier);
        finalColorTex->setState(after);
    }

    rp_color_tex_ = nullptr;
    rp_resolve_tex_ = nullptr;
}

}  // namespace mulan::engine
