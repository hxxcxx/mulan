#include "command_list.h"
#include "device.h"
#include "engine_error_code.h"
#include <mulan/core/log/log.h>

namespace mulan::engine {

core::Result<void> CommandList::begin() {
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
    active_bind_group_layout_ = nullptr;
    pending_bind_group_layout_ = nullptr;
    if (auto result = doBegin(); !result) {
        invalidate(result.error());
        return std::unexpected(result.error());
    }
    backend_recording_ = true;
    state_ = State::Recording;
    return {};
}

core::Result<void> CommandList::end() {
    if (recording_error_) {
        const core::Error error = *recording_error_;
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
        invalidate(result.error());
        return std::unexpected(result.error());
    }
    backend_recording_ = false;
    state_ = State::Executable;
    return {};
}

const core::Error* CommandList::recordingError() const noexcept {
    return recording_error_ ? &*recording_error_ : nullptr;
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
    assertRenderPassCompatible(info);
    if (auto result = doBeginRenderPass(info); !result) {
        invalidate(result.error());
        return;
    }
    render_pass_active_ = true;
}

void CommandList::endRenderPass() {
    if (state_ != State::Recording || !render_pass_active_) {
        invalidate(makeError(EngineErrorCode::CommandRecordingFailed, "endRenderPass requires an active render pass"));
        return;
    }
    doEndRenderPass();
    render_pass_active_ = false;
}

void CommandList::invalidate(core::Error error) {
    if (!recording_error_)
        recording_error_ = std::move(error);
    state_ = State::Invalid;
}

void CommandList::rejectRecording(std::string_view reason) {
    invalidate(makeError(EngineErrorCode::CommandRecordingFailed, reason));
}

void CommandList::activateBindGroupLayout(const BindGroupLayout& layout) {
    if (!active_bind_group_layout_ && pending_bind_group_layout_ && *pending_bind_group_layout_ != layout) {
        rejectRecording("BindGroup layout does not match the active pipeline");
    }
    active_bind_group_layout_ = &layout;
    pending_bind_group_layout_ = nullptr;
}

bool CommandList::validateBindGroupCompatible(const BindGroup& group) {
    if (state_ != State::Recording) {
        rejectRecording("BindGroup binding requires a recording CommandList");
        return false;
    }
    assertBindGroupCompatible(group);
    if (active_bind_group_layout_ && group.layout() != *active_bind_group_layout_) {
        invalidate(makeError(EngineErrorCode::CommandRecordingFailed,
                             "BindGroup layout does not match the active pipeline"));
        return false;
    }
    if (!active_bind_group_layout_)
        pending_bind_group_layout_ = &group.layout();
    return true;
}

void CommandList::bindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) {
    if (!dynamicUniforms.empty()) {
        LOG_ERROR("[RHI] Dynamic uniform binding is not implemented by the active backend");
        rejectRecording("Dynamic uniform binding is not implemented by the active backend");
        return;
    }
    bindGroup(group);
}

core::Result<UniformSlice> CommandList::writeUniformBytes(std::span<const std::byte>) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "Transient uniform allocation is not implemented"));
}

void CommandList::markSubmitted(SubmissionToken token) {
    assert(state_ == State::Executable);
    last_submission_ = token;
    state_ = State::Submitted;
}

core::Result<void> CommandList::waitForPreviousSubmission() {
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
