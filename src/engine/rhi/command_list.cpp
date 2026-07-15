#include "command_list.h"
#include "device.h"
#include "engine_error_code.h"
#include <mulan/core/log/log.h>

#include <string>
#include <algorithm>
#include <atomic>

namespace mulan::engine {

namespace {
std::atomic<uint64_t> g_next_descriptor_scope_id{ 1 };
}

CommandList::CommandList() : descriptor_scope_id_(g_next_descriptor_scope_id.fetch_add(1, std::memory_order_relaxed)) {
}

ResultVoid CommandList::begin() {
    if (state_ == State::Recording) {
        return std::unexpected(makeError(EngineErrorCode::CommandRecordingFailed, "CommandList is already recording"));
    }

    if (backend_recording_) {
        if (render_pass_active_)
            doEndRenderPass();
        if (auto cleanup = doEnd(); !cleanup) {
            invalidate(cleanup.error());
            return std::unexpected(cleanup.error());
        }
        backend_recording_ = false;
    }

    recording_error_.reset();
    render_pass_active_ = false;
    pipeline_kind_ = PipelineKind::None;
    push_constant_size_ = 0;
    active_bind_group_layout_.reset();
    active_graphics_pipeline_desc_.reset();
    active_render_pass_info_ = {};
    referenced_resources_.clear();
    if (auto result = doBegin(); !result) {
        const auto error = makeError(EngineErrorCode::CommandRecordingFailed, result.error().message);
        invalidate(error);
        return std::unexpected(error);
    }
    backend_recording_ = true;
    state_ = State::Recording;
    return {};
}

ResultVoid CommandList::end() {
    if (recording_error_) {
        const Error error = *recording_error_;
        if (backend_recording_) {
            if (render_pass_active_)
                doEndRenderPass();
            render_pass_active_ = false;
            (void) doEnd();
            backend_recording_ = false;
        }
        return std::unexpected(error);
    }
    if (state_ != State::Recording) {
        const auto error = makeError(EngineErrorCode::CommandRecordingFailed,
                                     "CommandList must be recording before it can be ended");
        invalidate(error);
        return std::unexpected(error);
    }
    if (render_pass_active_) {
        const auto error = makeError(EngineErrorCode::CommandRecordingFailed,
                                     "CommandList cannot end while a render pass is active");
        doEndRenderPass();
        render_pass_active_ = false;
        (void) doEnd();
        backend_recording_ = false;
        invalidate(error);
        return std::unexpected(error);
    }
    if (auto result = doEnd(); !result) {
        const auto error = makeError(EngineErrorCode::CommandRecordingFailed, result.error().message);
        invalidate(error);
        return std::unexpected(error);
    }
    backend_recording_ = false;
    state_ = State::Executable;
    return {};
}

const Error* CommandList::recordingError() const noexcept {
    return recording_error_ ? &*recording_error_ : nullptr;
}

bool CommandList::requireRecording(std::string_view operation) {
    if (state_ == State::Recording && !recording_error_)
        return true;
    rejectRecording(std::string(operation) + " requires a recording CommandList");
    return false;
}

bool CommandList::requireGraphicsDraw(std::string_view operation) {
    if (!requireRecording(operation))
        return false;
    if (!render_pass_active_) {
        rejectRecording(std::string(operation) + " requires an active render pass");
        return false;
    }
    if (pipeline_kind_ != PipelineKind::Graphics) {
        rejectRecording(std::string(operation) + " requires an active graphics pipeline");
        return false;
    }
    return true;
}

bool CommandList::requireComputeDispatch(std::string_view operation) {
    if (!requireRecording(operation))
        return false;
    if (render_pass_active_) {
        rejectRecording(std::string(operation) + " is not allowed inside a render pass");
        return false;
    }
    if (pipeline_kind_ != PipelineKind::Compute) {
        rejectRecording(std::string(operation) + " requires an active compute pipeline");
        return false;
    }
    return true;
}

bool CommandList::validateResource(const RHITrackedResource* resource, std::string_view operation) {
    if (!resource) {
        rejectRecording(std::string(operation) + " requires a non-null resource");
        return false;
    }
    RHIDevice* device = trackingDevice();
    if (device && resource->isTracked() && !resource->belongsTo(*device)) {
        rejectRecording(std::string(operation) + " received a resource from a different device");
        return false;
    }
    recordResource(resource);
    return true;
}

void CommandList::recordResource(const RHITrackedResource* resource) {
    if (!resource || !resource->lifetimeState())
        return;
    const auto& lifetime = resource->lifetimeState();
    const auto duplicate = std::find_if(referenced_resources_.begin(), referenced_resources_.end(),
                                        [&](const auto& current) { return current.get() == lifetime.get(); });
    if (duplicate == referenced_resources_.end())
        referenced_resources_.push_back(lifetime);
}

ResultVoid CommandList::validateReferencedResources() const {
    for (const auto& resource : referenced_resources_) {
        if (!resource || !resource->alive.load(std::memory_order_acquire)) {
            return std::unexpected(makeError(EngineErrorCode::SubmissionFailed,
                                             "CommandList references a resource destroyed after recording"));
        }
    }
    return {};
}

void CommandList::setPipelineState(PipelineState* pso) {
    if (!requireRecording("setPipelineState") || !validateResource(pso, "setPipelineState"))
        return;
    if (render_pass_active_ && !validateGraphicsPipelineRenderPass(pso->desc(), active_render_pass_info_))
        return;
    doSetPipelineState(pso);
    if (recording_error_)
        return;
    pipeline_kind_ = PipelineKind::Graphics;
    active_graphics_pipeline_desc_ = pso->desc();
    push_constant_size_ = pso->desc().pushConstantSize;
    activateBindGroupLayout(pso->bindGroupLayout());
}

void CommandList::setComputePipelineState(ComputePipelineState* pso) {
    if (!requireRecording("setComputePipelineState") || render_pass_active_) {
        if (render_pass_active_)
            rejectRecording("setComputePipelineState is not allowed inside a render pass");
        return;
    }
    if (!validateResource(pso, "setComputePipelineState"))
        return;
    doSetComputePipelineState(pso);
    if (recording_error_)
        return;
    pipeline_kind_ = PipelineKind::Compute;
    active_graphics_pipeline_desc_.reset();
    push_constant_size_ = pso->desc().pushConstantSize;
    activateBindGroupLayout(pso->bindGroupLayout());
}

void CommandList::bindGroup(BindGroup& group) {
    if (!validateBindGroupCompatible(group))
        return;
    doBindGroup(group);
}

void CommandList::bindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) {
    if (!validateBindGroupCompatible(group))
        return;
    for (const DynamicUniformBinding& binding : dynamicUniforms) {
        if (!validateResource(binding.slice.backingBuffer, "bindGroup"))
            return;
    }
    doBindGroup(group, dynamicUniforms);
}

Result<UniformSlice> CommandList::writeUniformBytes(std::span<const std::byte> data) {
    if (!requireRecording("writeUniformBytes"))
        return std::unexpected(*recording_error_);
    if (data.empty()) {
        rejectRecording("writeUniformBytes requires non-empty data");
        return std::unexpected(*recording_error_);
    }
    auto result = doWriteUniformBytes(data);
    if (!result)
        invalidate(result.error());
    return result;
}

void CommandList::setViewport(const Viewport& vp) {
    if (requireRecording("setViewport"))
        doSetViewport(vp);
}

void CommandList::setScissorRect(const ScissorRect& rect) {
    if (requireRecording("setScissorRect"))
        doSetScissorRect(rect);
}

void CommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    if (!requireRecording("setVertexBuffer") || !validateResource(buffer, "setVertexBuffer"))
        return;
    if (!(buffer->bindFlags() & BufferBindFlags::VertexBuffer) || offset >= buffer->size()) {
        rejectRecording("setVertexBuffer requires a VertexBuffer with an in-range offset");
        return;
    }
    doSetVertexBuffer(slot, buffer, offset);
}

void CommandList::setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
    if (!requireRecording("setVertexBuffers"))
        return;
    if (!buffers || count == 0) {
        rejectRecording("setVertexBuffers requires at least one buffer");
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!validateResource(buffers[i], "setVertexBuffers"))
            return;
        const uint32_t offset = offsets ? offsets[i] : 0;
        if (!(buffers[i]->bindFlags() & BufferBindFlags::VertexBuffer) || offset >= buffers[i]->size()) {
            rejectRecording("setVertexBuffers requires VertexBuffer resources with in-range offsets");
            return;
        }
    }
    doSetVertexBuffers(startSlot, count, buffers, offsets);
}

void CommandList::setIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    if (!requireRecording("setIndexBuffer") || !validateResource(buffer, "setIndexBuffer"))
        return;
    if (!(buffer->bindFlags() & BufferBindFlags::IndexBuffer) || offset >= buffer->size()) {
        rejectRecording("setIndexBuffer requires an IndexBuffer with an in-range offset");
        return;
    }
    doSetIndexBuffer(buffer, offset, type);
}

void CommandList::draw(const DrawAttribs& attribs) {
    if (requireGraphicsDraw("draw"))
        doDraw(attribs);
}

void CommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    if (requireGraphicsDraw("drawIndexed"))
        doDrawIndexed(attribs);
}

void CommandList::drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) {
    if (!requireGraphicsDraw("drawIndirect") || !validateResource(argsBuffer, "drawIndirect"))
        return;
    RHIDevice* device = trackingDevice();
    if (device && !device->capabilities().indirectDraw) {
        rejectRecording("drawIndirect is not supported by the active backend");
        return;
    }
    if (!(argsBuffer->bindFlags() & BufferBindFlags::IndirectBuffer) || offset >= argsBuffer->size()) {
        rejectRecording("drawIndirect requires an IndirectBuffer with an in-range offset");
        return;
    }
    doDrawIndirect(argsBuffer, offset, drawCount, stride);
}

void CommandList::dispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) {
    if (requireComputeDispatch("dispatch"))
        doDispatch(threadGroupX, threadGroupY, threadGroupZ);
}

void CommandList::dispatchIndirect(Buffer* argsBuffer, uint32_t offset) {
    if (!requireComputeDispatch("dispatchIndirect") || !validateResource(argsBuffer, "dispatchIndirect"))
        return;
    RHIDevice* device = trackingDevice();
    if (device && !device->capabilities().indirectDispatch) {
        rejectRecording("dispatchIndirect is not supported by the active backend");
        return;
    }
    if (!(argsBuffer->bindFlags() & BufferBindFlags::IndirectBuffer) || offset >= argsBuffer->size()) {
        rejectRecording("dispatchIndirect requires an IndirectBuffer with an in-range offset");
        return;
    }
    doDispatchIndirect(argsBuffer, offset);
}

void CommandList::setPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) {
    if (!requireRecording("setPushConstants"))
        return;
    RHIDevice* device = trackingDevice();
    if (pipeline_kind_ == PipelineKind::None || !data || size == 0 || (size & 3U) != 0 ||
        offset > push_constant_size_ || size > push_constant_size_ - offset || stageFlags == 0 ||
        (device && !device->capabilities().pushConstants)) {
        rejectRecording("setPushConstants received an unsupported or invalid constant range");
        return;
    }
    doSetPushConstants(offset, size, data, stageFlags);
}

void CommandList::transitionResource(Texture* texture, ResourceState newState) {
    if (!requireRecording("transitionResource") || render_pass_active_) {
        if (render_pass_active_)
            rejectRecording("transitionResource is not allowed inside a render pass");
        return;
    }
    if (!validateResource(texture, "transitionResource"))
        return;
    if (newState == ResourceState::VertexBuffer || newState == ResourceState::IndexBuffer ||
        newState == ResourceState::UniformBuffer) {
        rejectRecording("transitionResource received a buffer-only state for a texture");
        return;
    }
    doTransitionResource(texture, newState);
}

ResultVoid CommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    if (!requireRecording("copyTextureToBuffer") || render_pass_active_) {
        if (render_pass_active_)
            rejectRecording("copyTextureToBuffer is not allowed inside a render pass");
        return std::unexpected(*recording_error_);
    }
    if (!validateResource(src, "copyTextureToBuffer") || !validateResource(dst, "copyTextureToBuffer"))
        return std::unexpected(*recording_error_);
    if (dst->usage() != BufferUsage::Staging) {
        rejectRecording("copyTextureToBuffer requires a Staging destination buffer");
        return std::unexpected(*recording_error_);
    }
    auto result = doCopyTextureToBuffer(src, dst);
    if (!result)
        invalidate(result.error());
    return result;
}

void CommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    if (state_ != State::Recording || render_pass_active_) {
        invalidate(makeError(EngineErrorCode::CommandRecordingFailed,
                             "beginRenderPass requires a recording CommandList without an active render pass"));
        return;
    }
    if (info.colorCount > RenderPassBeginInfo::kMaxColorTargets) {
        rejectRecording("Render pass color attachment count exceeds the supported limit");
        return;
    }
    if (info.width == 0 || info.height == 0) {
        rejectRecording("Render pass dimensions must be non-zero");
        return;
    }
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        if ((info.colorAttachments[i].target &&
             !validateResource(info.colorAttachments[i].target, "beginRenderPass")) ||
            (info.colorAttachments[i].resolveTarget &&
             !validateResource(info.colorAttachments[i].resolveTarget, "beginRenderPass"))) {
            return;
        }
    }
    if ((info.depthAttachment.target && !validateResource(info.depthAttachment.target, "beginRenderPass")) ||
        (info.depthAttachment.resolveTarget &&
         !validateResource(info.depthAttachment.resolveTarget, "beginRenderPass"))) {
        return;
    }
    if (active_graphics_pipeline_desc_ && !validateGraphicsPipelineRenderPass(*active_graphics_pipeline_desc_, info))
        return;
    assertRenderPassCompatible(info);
    if (auto result = doBeginRenderPass(info); !result) {
        invalidate(result.error());
        return;
    }
    render_pass_active_ = true;
    active_render_pass_info_ = info;
}

void CommandList::endRenderPass() {
    if (state_ != State::Recording || !render_pass_active_) {
        invalidate(makeError(EngineErrorCode::CommandRecordingFailed, "endRenderPass requires an active render pass"));
        return;
    }
    doEndRenderPass();
    render_pass_active_ = false;
    active_render_pass_info_ = {};
}

bool CommandList::validateGraphicsPipelineRenderPass(const GraphicsPipelineDesc& desc,
                                                     const RenderPassBeginInfo& info) {
    if (desc.colorTargetCount != info.colorCount) {
        rejectRecording("Graphics pipeline color target count does not match the active render pass");
        return false;
    }
    uint32_t sampleCount = 0;
    for (uint8_t i = 0; i < info.colorCount; ++i) {
        const Texture* target = info.colorAttachments[i].target;
        if (!target || desc.colorFormats[i] != target->format()) {
            rejectRecording("Graphics pipeline color format does not match the active render pass");
            return false;
        }
        if (sampleCount == 0)
            sampleCount = target->desc().sampleCount;
        else if (sampleCount != target->desc().sampleCount) {
            rejectRecording("Render pass attachments use different sample counts");
            return false;
        }
    }
    const Texture* depthTarget = info.depthAttachment.target;
    const TextureFormat depthFormat = depthTarget ? depthTarget->format() : TextureFormat::Unknown;
    if (desc.depthStencilFormat != depthFormat) {
        rejectRecording("Graphics pipeline depth format does not match the active render pass");
        return false;
    }
    if (depthTarget) {
        if (sampleCount == 0)
            sampleCount = depthTarget->desc().sampleCount;
        else if (sampleCount != depthTarget->desc().sampleCount) {
            rejectRecording("Render pass color and depth attachments use different sample counts");
            return false;
        }
    }
    if (sampleCount == 0)
        sampleCount = 1;
    if (desc.sampleCount != sampleCount) {
        rejectRecording("Graphics pipeline sample count does not match the active render pass");
        return false;
    }
    return true;
}

void CommandList::invalidate(Error error) {
    if (!recording_error_)
        recording_error_ = std::move(error);
    state_ = State::Invalid;
}

void CommandList::rejectRecording(std::string_view reason) {
    invalidate(makeError(EngineErrorCode::CommandRecordingFailed, reason));
}

void CommandList::activateBindGroupLayout(const BindGroupLayout& layout) {
    active_bind_group_layout_ = layout;
}

bool CommandList::validateBindGroupCompatible(const BindGroup& group) {
    if (state_ != State::Recording) {
        rejectRecording("BindGroup binding requires a recording CommandList");
        return false;
    }
    if (pipeline_kind_ == PipelineKind::None || !active_bind_group_layout_) {
        rejectRecording("BindGroup binding requires an active pipeline");
        return false;
    }
    if (!validateResource(&group, "bindGroup"))
        return false;
    for (uint8_t i = 0; i < group.entryCount(); ++i) {
        const BindGroupEntry& entry = group.entries()[i];
        if ((entry.buffer && !validateResource(entry.buffer, "bindGroup")) ||
            (entry.texture && !validateResource(entry.texture, "bindGroup")) ||
            (entry.sampler && !validateResource(entry.sampler, "bindGroup"))) {
            return false;
        }
    }
    assertBindGroupCompatible(group);
    if (active_bind_group_layout_ && group.layout() != *active_bind_group_layout_) {
        invalidate(makeError(EngineErrorCode::CommandRecordingFailed,
                             "BindGroup layout does not match the active pipeline"));
        return false;
    }
    return true;
}

void CommandList::doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) {
    if (!dynamicUniforms.empty()) {
        LOG_ERROR("[RHI] Dynamic uniform binding is not implemented by the active backend");
        rejectRecording("Dynamic uniform binding is not implemented by the active backend");
        return;
    }
    doBindGroup(group);
}

Result<UniformSlice> CommandList::doWriteUniformBytes(std::span<const std::byte>) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "Transient uniform allocation is not implemented"));
}

void CommandList::markSubmitted(SubmissionToken token) {
    assert(state_ == State::Executable);
    for (const auto& resource : referenced_resources_) {
        if (!resource)
            continue;
        uint64_t observed = resource->lastSubmissionValue.load(std::memory_order_relaxed);
        while (observed < token.value &&
               !resource->lastSubmissionValue.compare_exchange_weak(observed, token.value, std::memory_order_release,
                                                                    std::memory_order_relaxed)) {}
    }
    doMarkSubmitted();
    last_submission_ = token;
    state_ = State::Submitted;
}

ResultVoid CommandList::waitForPreviousSubmission() {
    const SubmissionToken token = last_submission_;
    if (!token)
        return {};

    RHIDevice* device = trackingDevice();
    if (!device) {
        return std::unexpected(
                makeError(EngineErrorCode::InvalidSubmissionToken, "command list is detached from its device"));
    }
    if (device->isSubmissionComplete(token))
        return {};
    return device->waitForSubmission(token);
}

}  // namespace mulan::engine
