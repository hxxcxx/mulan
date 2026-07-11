#include "dx12_command_list.h"
#include "dx12_buffer.h"
#include "dx12_texture.h"
#include "dx12_pipeline_state.h"
#include "dx12_convert.h"
#include "dx12_bind_group.h"

#include <mulan/core/result/error.h>
#include "../../engine_error_code.h"

#include <cstdio>
#include <string>

namespace mulan::engine {

core::Result<std::unique_ptr<DX12CommandList>> DX12CommandList::create(ID3D12Device* device,
                                                                       ID3D12CommandAllocator* allocator) {
    try {
        return std::unique_ptr<DX12CommandList>(new DX12CommandList(device, allocator));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(EngineErrorCode::CommandListCreateFailed,
                                         std::string("DX12CommandList create failed: ") + e.what()));
    }
}

DX12CommandList::DX12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator) : owns_cmd_list_(true) {
    HRESULT hr =
            device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&cmd_list_));
    DX12_CHECK(hr);
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
}

void DX12CommandList::begin() {
    // 帧循环模式：cmd list 已由 DX12FrameContext::resetCommandAllocator() Reset
    // 独立模式：需要外部 Reset allocator 后调用
}

void DX12CommandList::end() {
    HRESULT hr = cmd_list_->Close();
    DX12_CHECK(hr);
}

void DX12CommandList::setPipelineState(PipelineState* pso) {
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
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = dx12Buf->gpuAddress() + offset;
    vbv.SizeInBytes = buffer->size() - offset;
    vbv.StrideInBytes = cached_stride_;
    cmd_list_->IASetVertexBuffers(slot, 1, &vbv);
}

void DX12CommandList::setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
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
    if (!draw_indirect_sig_)
        return;
    auto* dx12Buf = static_cast<DX12Buffer*>(argsBuffer);
    cmd_list_->ExecuteIndirect(draw_indirect_sig_, drawCount, dx12Buf->resource(), offset, nullptr, 0);
}

void DX12CommandList::dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
    cmd_list_->Dispatch(threadGroupX, threadGroupY, threadGroupZ);
}

void DX12CommandList::dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
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
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    dx12Buf->update(offset, size, data);
}

void DX12CommandList::bindGroup(BindGroup& group) {
    auto* dx12Group = static_cast<DX12BindGroup*>(&group);
    if (dx12Group->entryCount() == 0 || !desc_heap_)
        return;

    // --- 跨帧失效：per-frame heap reset 已回收上一帧的 descriptor 区段，
    // 若 BindGroup 缓存句柄不属于当前帧则丢弃并强制本帧完整重写。
    if (dx12Group->frameToken() != frame_token_) {
        dx12Group->setCachedGpuHandle({});
        dx12Group->setFrameToken(frame_token_);
        dx12Group->markAllDirty();
    }

    // 统计 texture binding 数：每个 texture 占 descriptor heap 的一个独立 slot。
    // 与 Vulkan 的 descriptor set 不同，DX12 每个 SRV root parameter 指向独立的
    // 1-descriptor table，因此必须为每个 texture 分配独立的 descriptor 槽，
    // 否则多个 texture 会互相覆盖（表现为贴图丢失）。
    uint32_t texCount = 0;
    for (uint8_t i = 0; i < dx12Group->entryCount(); ++i) {
        if (dx12Group->entries()[i].texture)
            ++texCount;
    }

    // --- 分配或复用一段连续 descriptor 区段（texCount 个 slot）---
    // 区段起点缓存于 BindGroup；跨帧/首帧失效后重新分配。
    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = dx12Group->cachedGpuHandle();
    if (!gpuBase.ptr) {
        if (desc_alloc_count_ + texCount > 1024) {
            std::fprintf(stderr, "[DX12 bindGroup] shader-visible descriptor heap exhausted\n");
            return;
        }
        gpuBase.ptr = desc_gpu_base_.ptr + desc_alloc_count_ * desc_size_;
        desc_alloc_count_ += texCount;
        dx12Group->setCachedGpuHandle(gpuBase);
    }

    ID3D12Device* device = nullptr;
    cmd_list_->GetDevice(IID_PPV_ARGS(&device));

    // 遍历 entries，按类型绑定：
    //   UBO     → root CBV（直接 GPU 地址，无需 descriptor heap）
    //   Texture → 独立 descriptor slot（区段内第 texSlot 个），脏时重写
    //   Sampler → 跳过（由 root signature 的 static sampler 提供）
    const uint16_t mask = dx12Group->dirtyMask();
    uint16_t written = 0;
    uint32_t texSlot = 0;

    for (uint8_t i = 0; i < dx12Group->entryCount(); ++i) {
        const auto& e = dx12Group->entries()[i];
        uint32_t rootIdx = dx12Group->rootIndexForBinding(e.binding);
        if (rootIdx == DX12BindGroup::kInvalidRootIndex)
            continue;  // Sampler：跳过

        if (e.buffer) {
            // UBO：root CBV 每 draw 必须重设（offset 变化），不依赖脏位
            auto* dx12Buf = static_cast<DX12Buffer*>(e.buffer);
            cmd_list_->SetGraphicsRootConstantBufferView(rootIdx, dx12Buf->gpuAddress() + e.offset);
        } else if (e.texture) {
            // 当前 texture 在区段中的 GPU/CPU handle
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
            gpuHandle.ptr = gpuBase.ptr + texSlot * desc_size_;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            cpuHandle.ptr = desc_cpu_base_.ptr + (gpuHandle.ptr - desc_gpu_base_.ptr);

            // 仅脏时重写 descriptor 槽，非脏复用上一轮结果
            if (((mask >> i) & 1u) != 0) {
                auto* dx12Tex = static_cast<DX12Texture*>(e.texture);
                if (dx12Tex && dx12Tex->srv().ptr && device) {
                    device->CopyDescriptorsSimple(1, cpuHandle, dx12Tex->srv(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
                written |= (uint16_t(1) << i);
            }
            cmd_list_->SetGraphicsRootDescriptorTable(rootIdx, gpuHandle);
            ++texSlot;
        }
    }

    if (device)
        device->Release();
    dx12Group->clearDirty(written);
}

void DX12CommandList::bindResources(const BindGroupDesc& desc) {
    // 注意：此便捷路径仅接收 BindGroupDesc（无 BindGroupLayout），无法得知各 binding
    // 的 DescriptorType，因此无法做 binding→root-parameter-index 映射。当前引擎渲染
    // 路径一律走 bindGroup(BindGroup&)（后者有完整映射）。若将来启用此路径且 PSO 含
    // static sampler，需改为接收 layout 或 PSO 以正确计算 root index。
    for (uint8_t i = 0; i < desc.count; ++i) {
        const auto& e = desc.entries[i];
        if (e.buffer) {
            auto* dx12Buf = static_cast<DX12Buffer*>(e.buffer);
            cmd_list_->SetGraphicsRootConstantBufferView(e.binding, dx12Buf->gpuAddress() + e.offset);
        } else if (e.texture && desc_heap_) {
            auto* dx12Tex = static_cast<DX12Texture*>(e.texture);
            if (!dx12Tex->srv().ptr)
                continue;

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
                                        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase, uint32_t descriptorSize) {
    desc_heap_ = heap;
    desc_cpu_base_ = cpuBase;
    desc_gpu_base_ = gpuBase;
    desc_size_ = descriptorSize;
    desc_alloc_count_ = 0;

    // 把 shader-visible descriptor heap 绑定到命令列表。
    // D3D12 要求：使用 root descriptor table 引用 GPU handle 前，必须先
    // SetDescriptorHeaps，否则 descriptor table 引用的 GPU handle 无效。
    // 每帧 beginFrame 会 reset heap 后重新调用此函数，此处随之重新绑定。
    if (heap && cmd_list_) {
        ID3D12DescriptorHeap* heaps[] = { heap };
        cmd_list_->SetDescriptorHeaps(1, heaps);
    }
}

void DX12CommandList::transitionResource(Buffer* buffer, ResourceState newState) {
    // 未实现：buffer 资源状态当前未跟踪（DX12Buffer 无 state_ 字段，与 Texture 不同）。
    // 之前的实现把 StateBefore 写死 GENERIC_READ，对 staging/COPY_DEST 等是错的。
    // 当前无调用方；要正确实现需先给 DX12Buffer 加状态跟踪。
    (void) buffer;
    (void) newState;
    assert(false &&
           "transitionResource(Buffer*) not implemented: "
           "DX12Buffer lacks per-resource state tracking");
}

void DX12CommandList::transitionResource(Texture* texture, ResourceState newState) {
    auto* dx12Tex = static_cast<DX12Texture*>(texture);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dx12Tex->resource();
    barrier.Transition.StateBefore = dx12Tex->state();
    barrier.Transition.StateAfter = toDX12ResourceStates(newState);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &barrier);
    dx12Tex->setState(toDX12ResourceStates(newState));
}

void DX12CommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    auto* dx12Tex = static_cast<DX12Texture*>(src);
    auto* dx12Buf = static_cast<DX12Buffer*>(dst);

    // 取 device 用于 GetCopyableFootprints（与 bindResources 同模式）
    ID3D12Device* device = nullptr;
    cmd_list_->GetDevice(IID_PPV_ARGS(&device));
    if (!device)
        return;

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

    // src texture: COMMON/COPY_DEST → COPY_SOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dx12Tex->resource();
    barrier.Transition.StateBefore = dx12Tex->state();
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &barrier);

    // dst buffer: GENERIC_READ → COPY_DEST
    D3D12_RESOURCE_BARRIER bufBarrier = {};
    bufBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bufBarrier.Transition.pResource = dx12Buf->resource();
    bufBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    bufBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    bufBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &bufBarrier);

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.pResource = dx12Tex->resource();
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.pResource = dx12Buf->resource();
    dstLoc.PlacedFootprint = footprint;

    cmd_list_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // 还原状态
    bufBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bufBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    cmd_list_->ResourceBarrier(1, &bufBarrier);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = dx12Tex->state();
    cmd_list_->ResourceBarrier(1, &barrier);
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
