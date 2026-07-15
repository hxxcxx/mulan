#include "detail/dx12_command_list.h"
#include "detail/dx12_buffer.h"
#include "detail/dx12_texture.h"
#include "detail/dx12_pipeline_state.h"
#include "detail/dx12_convert.h"
#include "detail/dx12_bind_group.h"
#include "detail/dx12_sampler.h"
#include "detail/dx12_transient_uniform_arena.h"
#include "detail/dx12_descriptor_allocator.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <algorithm>
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
    : allocator_(allocator),
      owns_cmd_list_(true),
      owned_transient_uniform_arena_(std::make_unique<DX12TransientUniformArena>(device)),
      transient_uniform_arena_(owned_transient_uniform_arena_.get()) {
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
    if (owns_cmd_list_) {
        if (auto wait = waitForPreviousSubmission(); !wait)
            LOG_ERROR("[DX12] 销毁独立命令列表前等待提交失败：{}", wait.error().message);
    }
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

core::Result<void> DX12CommandList::doBegin() {
    if (!cmd_list_) {
        return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed, "DX12 command list is unavailable"));
    }

    pending_texture_states_.clear();
    if (owns_cmd_list_) {
        // 独立命令列表由 create() 先关闭；begin() 负责复用 allocator 并重新打开。
        if (recording_ || !allocator_) {
            return std::unexpected(
                    makeError(EngineErrorCode::CommandRecordingFailed, "DX12 command list cannot be reset"));
        }
        const auto previousSubmission = waitForPreviousSubmission();
        if (!previousSubmission)
            return std::unexpected(previousSubmission.error());
        if (!checkDX12(allocator_->Reset(), "ID3D12CommandAllocator::Reset")) {
            return std::unexpected(
                    makeError(EngineErrorCode::CommandRecordingFailed, "DX12 command allocator reset failed"));
        }
        if (!checkDX12(cmd_list_->Reset(allocator_.Get(), nullptr), "ID3D12GraphicsCommandList::Reset")) {
            return std::unexpected(
                    makeError(EngineErrorCode::CommandRecordingFailed, "DX12 command list reset failed"));
        }
        if (owned_descriptor_arena_) {
            // 上一次提交已经完成，可以安全复用独立命令列表私有的描述符 heap。
            owned_descriptor_arena_->reset();
            desc_alloc_count_ = 0;
            ++frame_token_;
        }
    }

    // 帧循环模式的 allocator/list 已由 DX12FrameContext reset；这里仅标记录制状态。
    recording_ = true;
    if (transient_uniform_arena_)
        transient_uniform_arena_->beginRecording();
    bindDescriptorHeaps();
    return {};
}

core::Result<void> DX12CommandList::doEnd() {
    if (!cmd_list_ || !recording_) {
        return std::unexpected(
                makeError(EngineErrorCode::CommandRecordingFailed, "DX12 command list is not recording"));
    }
    HRESULT hr = cmd_list_->Close();
    if (transient_uniform_arena_)
        transient_uniform_arena_->endRecording();
    if (!checkDX12(hr, "ID3D12GraphicsCommandList::Close")) {
        recording_ = false;
        return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed, "DX12 command list close failed"));
    }
    recording_ = false;
    return {};
}

void DX12CommandList::doSetPipelineState(PipelineState* pso) {
    assertResourceCompatible(pso);
    if (!pso) {
        rejectRecording("DX12 graphics pipeline is null");
        return;
    }
    auto* dx12Pso = static_cast<DX12PipelineState*>(pso);
    activateBindGroupLayout(pso->bindGroupLayout());
    cmd_list_->SetPipelineState(dx12Pso->pipeline());
    cmd_list_->SetGraphicsRootSignature(dx12Pso->rootSignature());
    cmd_list_->IASetPrimitiveTopology(toDX12Topology(pso->desc().topology));
    cached_stride_ = pso->desc().vertexLayout.stride();
}

void DX12CommandList::doSetViewport(const Viewport& vp) {
    D3D12_VIEWPORT d3dVp = { vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
    cmd_list_->RSSetViewports(1, &d3dVp);
}

void DX12CommandList::doSetScissorRect(const ScissorRect& rect) {
    D3D12_RECT d3dRect = { rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
    cmd_list_->RSSetScissorRects(1, &d3dRect);
}

void DX12CommandList::doSetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    assertResourceCompatible(buffer);
    if (!buffer || offset >= buffer->size()) {
        rejectRecording("DX12 vertex-buffer binding is invalid");
        return;
    }
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = dx12Buf->gpuAddress() + offset;
    vbv.SizeInBytes = buffer->size() - offset;
    vbv.StrideInBytes = cached_stride_;
    cmd_list_->IASetVertexBuffers(slot, 1, &vbv);
}

void DX12CommandList::doSetVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
    if (!buffers || count > 16 || startSlot >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT ||
        count > D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - startSlot) {
        rejectRecording("DX12 vertex-buffer array exceeds the RHI limit");
        return;
    }
    for (uint32_t i = 0; i < count; ++i)
        assertResourceCompatible(buffers[i]);
    D3D12_VERTEX_BUFFER_VIEW vbvs[16] = {};
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = offsets ? offsets[i] : 0;
        if (!buffers[i] || offset >= buffers[i]->size()) {
            rejectRecording("DX12 vertex-buffer array contains an invalid buffer or offset");
            return;
        }
        auto* dx12Buf = static_cast<DX12Buffer*>(buffers[i]);
        vbvs[i].BufferLocation = dx12Buf->gpuAddress() + offset;
        vbvs[i].SizeInBytes = buffers[i]->size() - offset;
        vbvs[i].StrideInBytes = cached_stride_;
    }
    cmd_list_->IASetVertexBuffers(startSlot, count, vbvs);
}

void DX12CommandList::doSetIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    assertResourceCompatible(buffer);
    if (!buffer || offset >= buffer->size()) {
        rejectRecording("DX12 index-buffer binding is invalid");
        return;
    }
    auto* dx12Buf = static_cast<DX12Buffer*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = dx12Buf->gpuAddress() + offset;
    ibv.SizeInBytes = buffer->size() - offset;
    ibv.Format = (type == IndexType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    cmd_list_->IASetIndexBuffer(&ibv);
}

void DX12CommandList::doDraw(const DrawAttribs& attribs) {
    cmd_list_->DrawInstanced(attribs.vertexCount, attribs.instanceCount, attribs.startVertex, attribs.startInstance);
}

void DX12CommandList::doDrawIndexed(const DrawIndexedAttribs& attribs) {
    cmd_list_->DrawIndexedInstanced(attribs.indexCount, attribs.instanceCount, attribs.startIndex, attribs.baseVertex,
                                    attribs.startInstance);
}

void DX12CommandList::doDrawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) {
    assertResourceCompatible(argsBuffer);
    if (!draw_indirect_sig_) {
        LOG_ERROR("[DX12] drawIndirect rejected: command signature is unavailable");
        rejectRecording("DX12 indirect-draw command signature is unavailable");
        return;
    }
    if (stride != 0 && stride != sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)) {
        LOG_ERROR("[DX12] drawIndirect rejected: custom argument stride is not supported");
        rejectRecording("DX12 indirect-draw stride is unsupported");
        return;
    }
    auto* dx12Buf = static_cast<DX12Buffer*>(argsBuffer);
    const uint64_t requiredSize =
            static_cast<uint64_t>(offset) + static_cast<uint64_t>(drawCount) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    if (!dx12Buf || !dx12Buf->resource() || drawCount == 0 || requiredSize > dx12Buf->size()) {
        rejectRecording("DX12 indirect-draw arguments are invalid");
        return;
    }
    cmd_list_->ExecuteIndirect(draw_indirect_sig_, drawCount, dx12Buf->resource(), offset, nullptr, 0);
}

void DX12CommandList::doDispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
    (void) threadGroupX;
    (void) threadGroupY;
    (void) threadGroupZ;
    LOG_ERROR("[DX12] dispatch rejected: compute pipeline is not implemented");
    rejectRecording("DX12 compute dispatch is not implemented");
}

void DX12CommandList::doDispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    assertResourceCompatible(argsBuffer);
    (void) offset;
    LOG_ERROR("[DX12] dispatchIndirect rejected: compute pipeline is not implemented");
    rejectRecording("DX12 indirect compute dispatch is not implemented");
}

void DX12CommandList::doSetPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) {
    (void) offset;
    (void) size;
    (void) data;
    (void) stageFlags;
    LOG_ERROR("[DX12] setPushConstants rejected: root constants are not implemented");
    rejectRecording("DX12 root constants are not implemented");
}

void DX12CommandList::doBindGroup(BindGroup& group) {
    auto* dx12Group = static_cast<DX12BindGroup*>(&group);
    if (std::any_of(dx12Group->layout().entries().begin(), dx12Group->layout().entries().end(),
                    [](const BindGroupLayoutEntry& entry) { return entry.mode == BindingMode::Dynamic; })) {
        LOG_ERROR("[DX12] bindGroup rejected: dynamic UniformBuffer bindings are required by the layout");
        rejectRecording("DX12 bindGroup requires dynamic UniformBuffer bindings");
        return;
    }
    bindStaticGroup(*dx12Group);
}

void DX12CommandList::doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) {
    auto* dx12Group = static_cast<DX12BindGroup*>(&group);
    const uint64_t generation = transient_uniform_arena_ ? transient_uniform_arena_->recordingGeneration() : 0;
    const std::string validationError =
            validateDynamicUniformBindings(dx12Group->layout(), dynamicUniforms,
                                           { D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, 64 * 1024 }, generation);
    if (!validationError.empty()) {
        LOG_ERROR("[DX12] Dynamic uniform binding rejected: {}", validationError);
        rejectRecording(validationError);
        return;
    }

    bindStaticGroup(*dx12Group);
    for (const auto& binding : dynamicUniforms) {
        assertResourceCompatible(binding.slice.backingBuffer);
        auto* buffer = static_cast<DX12Buffer*>(binding.slice.backingBuffer);
        const uint32_t rootIndex = dx12Group->rootIndexForBinding(binding.binding);
        if (!buffer || !buffer->resource() || rootIndex == DX12BindGroup::kInvalidRootIndex) {
            LOG_ERROR("[DX12] Dynamic uniform binding {} does not reference a DX12 upload page", binding.binding);
            rejectRecording("DX12 dynamic uniform binding does not reference an upload page");
            return;
        }
        cmd_list_->SetGraphicsRootConstantBufferView(rootIndex, buffer->gpuAddress() + binding.slice.offset);
    }
}

core::Result<UniformSlice> DX12CommandList::doWriteUniformBytes(std::span<const std::byte> data) {
    if (!transient_uniform_arena_)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 transient uniform arena is unavailable"));
    const auto allocation = transient_uniform_arena_->upload(data);
    if (!allocation)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "DX12 transient uniform allocation failed"));
    return UniformSlice{ allocation.backingBuffer, allocation.offset, allocation.size, allocation.recordingGeneration };
}

void DX12CommandList::bindStaticGroup(DX12BindGroup& group) {
    auto* dx12Group = &group;
    if (dx12Group->entryCount() == 0)
        return;

    // --- 跨帧失效：per-frame heap reset 已回收上一帧的 descriptor 区段，
    // 若 BindGroup 缓存的 descriptor handles 不属于当前帧则丢弃并强制本帧完整重写。
    const DescriptorCacheEpoch epoch = descriptorCacheEpoch(frame_token_);
    if (dx12Group->cacheEpoch() != epoch) {
        dx12Group->clearCachedTextureHandles();
        dx12Group->setCacheEpoch(epoch);
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
        rejectRecording("DX12 shader-visible descriptor heap is not bound");
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
        if (rootIdx == DX12BindGroup::kInvalidRootIndex) {
            rejectRecording("DX12 BindGroup binding has no root parameter");
            return;
        }

        if (e.type == DescriptorType::UniformBuffer && e.buffer) {
            // UBO：root CBV 每 draw 必须重设（offset 变化），不依赖脏位
            auto* dx12Buf = static_cast<DX12Buffer*>(e.buffer);
            if (!dx12Buf || !dx12Buf->resource()) {
                rejectRecording("DX12 uniform-buffer binding is invalid");
                return;
            }
            cmd_list_->SetGraphicsRootConstantBufferView(rootIdx, dx12Buf->gpuAddress() + e.offset);
        } else if (e.type == DescriptorType::TextureSRV && e.texture) {
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12Group->cachedTextureHandle(i);
            const bool descriptorDirty = ((mask >> i) & 1u) != 0;
            if (!gpuHandle.ptr || descriptorDirty) {
                auto* dx12Tex = static_cast<DX12Texture*>(e.texture);
                if (!dx12Tex || !dx12Tex->srv().ptr || !device) {
                    LOG_ERROR("[DX12] bindGroup rejected: texture has no valid SRV");
                    rejectRecording("DX12 texture binding has no valid SRV");
                    return;
                }
                if (desc_alloc_count_ >= desc_capacity_) {
                    LOG_ERROR("[DX12] bindGroup rejected: shader-visible descriptor heap exhausted");
                    rejectRecording("DX12 shader-visible descriptor heap is exhausted");
                    return;
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
                rejectRecording("DX12 sampler descriptor is not shader-visible");
                return;
            }

            // Sampler descriptor 在创建时写入持久的 shader-visible heap，绑定时无需逐帧复制。
            cmd_list_->SetGraphicsRootDescriptorTable(rootIdx, samplerHandle);
            written |= (uint16_t(1) << i);
        }
    }

    if (device)
        device->Release();
    dx12Group->clearDirty(written);
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

D3D12_RESOURCE_STATES DX12CommandList::textureState(DX12Texture* texture) const {
    const auto it = pending_texture_states_.find(texture);
    return it != pending_texture_states_.end() ? it->second : texture->state();
}

void DX12CommandList::setTextureState(DX12Texture* texture, D3D12_RESOURCE_STATES state) {
    pending_texture_states_[texture] = state;
}

void DX12CommandList::doMarkSubmitted() {
    for (const auto& [texture, state] : pending_texture_states_) {
        if (texture)
            texture->setState(state);
    }
    pending_texture_states_.clear();
}

void DX12CommandList::setOwnedDescriptorArena(std::unique_ptr<DX12DescriptorAllocator> arena,
                                              ID3D12DescriptorHeap* samplerHeap) {
    owned_descriptor_arena_ = std::move(arena);
    if (!owned_descriptor_arena_ || !owned_descriptor_arena_->isValid()) {
        setDescriptorHeap(nullptr, {}, {}, 0, samplerHeap, 0);
        return;
    }
    auto* heap = owned_descriptor_arena_->heap();
    setDescriptorHeap(heap, heap->GetCPUDescriptorHandleForHeapStart(), heap->GetGPUDescriptorHandleForHeapStart(),
                      owned_descriptor_arena_->descriptorSize(), samplerHeap, owned_descriptor_arena_->capacity());
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

void DX12CommandList::doTransitionResource(Texture* texture, ResourceState newState) {
    assertResourceCompatible(texture);
    auto* dx12Tex = static_cast<DX12Texture*>(texture);
    if (!dx12Tex || !dx12Tex->resource()) {
        rejectRecording("DX12 texture transition requires a valid texture");
        return;
    }

    const D3D12_RESOURCE_STATES before = textureState(dx12Tex);
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
    setTextureState(dx12Tex, after);
}

core::Result<void> DX12CommandList::doCopyTextureToBuffer(Texture* src, Buffer* dst) {
    assertResourceCompatible(src);
    assertResourceCompatible(dst);
    const auto rejectCopy = [this](std::string_view reason) -> core::Result<void> {
        rejectRecording(reason);
        return std::unexpected(makeError(EngineErrorCode::ResourceReadbackFailed, reason));
    };
    auto* dx12Tex = static_cast<DX12Texture*>(src);
    auto* dx12Buf = static_cast<DX12Buffer*>(dst);
    if (!dx12Tex || !dx12Buf || !dx12Tex->resource() || !dx12Buf->resource())
        return rejectCopy("DX12 texture copy requires valid resources");

    // 取 device 用于 GetCopyableFootprints。
    ID3D12Device* device = nullptr;
    cmd_list_->GetDevice(IID_PPV_ARGS(&device));
    if (!device)
        return rejectCopy("DX12 texture copy could not query the device");

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
        return rejectCopy("DX12 readback buffer is too small");
    }

    const D3D12_RESOURCE_STATES originalTexState = textureState(dx12Tex);
    const D3D12_RESOURCE_STATES originalBufState = dx12Buf->state();

    if (originalBufState != D3D12_RESOURCE_STATE_COPY_DEST &&
        (dx12Buf->usage() == BufferUsage::Staging || dx12Buf->usage() == BufferUsage::Dynamic)) {
        LOG_ERROR("[DX12] copyTextureToBuffer rejected: destination buffer is not in COPY_DEST state");
        return rejectCopy("DX12 destination buffer is not in copy-destination state");
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
        setTextureState(dx12Tex, D3D12_RESOURCE_STATE_COPY_SOURCE);
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
        setTextureState(dx12Tex, originalTexState);
    }
    return {};
}

void DX12CommandList::doSetComputePipelineState(ComputePipelineState*) {
    LOG_ERROR("[DX12] Compute pipeline binding rejected: compute is not implemented");
    rejectRecording("DX12 compute pipelines are not implemented");
}

// ============================================================
// RenderPass
// ============================================================

core::Result<void> DX12CommandList::doBeginRenderPass(const RenderPassBeginInfo& info) {
    auto* cl = cmd_list_.Get();
    if (!cl || info.colorCount > RenderPassBeginInfo::kMaxColorTargets || info.width == 0 || info.height == 0)
        return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                         "DX12 render pass dimensions or attachment count are invalid"));
    rp_present_source_ = info.presentSource;
    rp_color_count_ = info.colorCount;
    rp_color_textures_.fill(nullptr);
    rp_resolve_textures_.fill(nullptr);

    uint32_t attachmentSampleCount = 0;
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* texture = static_cast<DX12Texture*>(info.colorAttachments[i].target);
        if (!texture || !texture->resource())
            return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                             "DX12 render pass requires valid color textures"));
        if (attachmentSampleCount == 0)
            attachmentSampleCount = texture->desc().sampleCount;
        else if (attachmentSampleCount != texture->desc().sampleCount)
            return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                             "DX12 render pass attachments must use the same sample count"));

        if (auto* resolveTexture = static_cast<DX12Texture*>(info.colorAttachments[i].resolveTarget)) {
            if (!resolveTexture->resource() || texture->desc().sampleCount <= 1 ||
                resolveTexture->desc().sampleCount != 1 || resolveTexture->format() != texture->format() ||
                resolveTexture->width() != texture->width() || resolveTexture->height() != texture->height()) {
                return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                                 "DX12 resolve target is incompatible with its color texture"));
            }
        }
    }
    if (auto* depthTexture = static_cast<DX12Texture*>(info.depthAttachment.target)) {
        if (!depthTexture->resource() ||
            (attachmentSampleCount != 0 && attachmentSampleCount != depthTexture->desc().sampleCount)) {
            return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed,
                                             "DX12 depth attachment is invalid or uses a different sample count"));
        }
    }

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, RenderPassBeginInfo::kMaxColorTargets> rtvHandles{};

    // Color attachment barrier: current → RENDER_TARGET
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        auto* tex = static_cast<DX12Texture*>(info.colorAttachments[i].target);
        rp_color_textures_[i] = tex;
        rtvHandles[i] = tex->rtv();

        D3D12_RESOURCE_STATES before = textureState(tex);
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (tex->resource() && before != after) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = tex->resource();
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
            setTextureState(tex, after);
        }

        // Clear
        if (info.colorAttachments[i].loadAction == LoadAction::Clear) {
            cl->ClearRenderTargetView(tex->rtv(), info.clearColor, 0, nullptr);
        }

        if (info.colorAttachments[i].resolveTarget) {
            auto* resolveTex = static_cast<DX12Texture*>(info.colorAttachments[i].resolveTarget);
            rp_resolve_textures_[i] = resolveTex;
            D3D12_RESOURCE_STATES resolveBefore = textureState(resolveTex);
            D3D12_RESOURCE_STATES resolveAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
            if (resolveTex->resource() && resolveBefore != resolveAfter) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = resolveTex->resource();
                barrier.Transition.StateBefore = resolveBefore;
                barrier.Transition.StateAfter = resolveAfter;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cl->ResourceBarrier(1, &barrier);
                setTextureState(resolveTex, resolveAfter);
            }
        }
    }

    // Depth attachment
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = nullptr;
    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<DX12Texture*>(info.depthAttachment.target);

        D3D12_RESOURCE_STATES before = textureState(depthTex);
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if (depthTex->resource() && before != after) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = depthTex->resource();
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
            setTextureState(depthTex, after);
        }

        if (info.depthAttachment.loadAction == LoadAction::Clear) {
            D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH;
            if (depthTex->format() == TextureFormat::D24_UNorm_S8_UInt ||
                depthTex->format() == TextureFormat::D32_Float_S8X24_UInt) {
                clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
            }
            cl->ClearDepthStencilView(depthTex->dsv(), clearFlags, info.clearDepth, info.clearStencil, 0, nullptr);
        }
        dsvHandle = depthTex->dsv();
        pDSV = &dsvHandle;
    }

    cl->OMSetRenderTargets(info.colorCount, info.colorCount > 0 ? rtvHandles.data() : nullptr, FALSE, pDSV);
    return {};
}

void DX12CommandList::doEndRenderPass() {
    auto* cl = cmd_list_.Get();
    for (uint8_t i = 0; i < rp_color_count_; ++i) {
        DX12Texture* colorTexture = rp_color_textures_[i];
        DX12Texture* resolveTexture = rp_resolve_textures_[i];
        if (!colorTexture)
            continue;

        DX12Texture* finalColorTexture = resolveTexture ? resolveTexture : colorTexture;
        if (resolveTexture) {
            const D3D12_RESOURCE_STATES beforeResolve = textureState(colorTexture);
            if (beforeResolve != D3D12_RESOURCE_STATE_RESOLVE_SOURCE) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = colorTexture->resource();
                barrier.Transition.StateBefore = beforeResolve;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cl->ResourceBarrier(1, &barrier);
                setTextureState(colorTexture, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            }
            cl->ResolveSubresource(resolveTexture->resource(), 0, colorTexture->resource(), 0,
                                   toDXGIFormat(resolveTexture->format()));
        }

        const D3D12_RESOURCE_STATES targetState = rp_present_source_ && i == 0
                                                          ? D3D12_RESOURCE_STATE_PRESENT
                                                          : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES before = textureState(finalColorTexture);
        if (before != targetState) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = finalColorTexture->resource();
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = targetState;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
            setTextureState(finalColorTexture, targetState);
        }
    }

    rp_color_count_ = 0;
    rp_color_textures_.fill(nullptr);
    rp_resolve_textures_.fill(nullptr);
}

}  // namespace mulan::engine
