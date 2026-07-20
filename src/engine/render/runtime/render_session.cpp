#include "render_session.h"

#include "detail/render_channel.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include "../../rhi/engine_error_code.h"

#include <cassert>
#include <string_view>
#include <utility>

namespace mulan::engine {
namespace {

Error sessionError(ErrorCode code, std::string_view message) {
    return Error::make(code, message);
}

}  // namespace

RenderSession::RenderSession() : owner_thread_(std::this_thread::get_id()) {
}

RenderSession::~RenderSession() {
    shutdown();
}

ResultVoid RenderSession::init(const RenderSessionConfig& config, int width, int height,
                               std::function<void()> runtimeEventCallback) {
    MULAN_PROFILE_ZONE();
    assertOwnerThread();
    if (isReady())
        return {};
    discardChannel();

    auto candidate = std::make_unique<detail::RenderChannel>();
    if (auto initialized = candidate->init(config, width, height, std::move(runtimeEventCallback)); !initialized)
        return initialized;
    channel_ = std::move(candidate);
    last_runtime_failure_.reset();
    return {};
}

void RenderSession::shutdown() {
    assertOwnerThread();
    discardChannel();
    last_runtime_failure_.reset();
}

bool RenderSession::isReady() const {
    assertOwnerThread();
    return channel_ && channel_->isReady();
}

Result<std::optional<uint64_t>> RenderSession::consumeRuntimeEvents() {
    assertOwnerThread();
    if (last_runtime_failure_)
        return std::unexpected(*last_runtime_failure_);
    if (!channel_)
        return std::optional<uint64_t>{};

    const std::optional<uint64_t> completed = channel_->takeCompletedResourceBatch();
    if (const std::optional<Error> failure = channel_->failureSnapshot()) {
        const Error error = *failure;
        failExecution(error);
        return std::unexpected(error);
    }
    if (!channel_->isReady()) {
        const Error error = sessionError(ErrorCode::Internal, "Render channel stopped without a failure snapshot.");
        failExecution(error);
        return std::unexpected(error);
    }
    return completed;
}

ResultVoid RenderSession::submitFrame(RenderFrameSubmission submission) {
    assertOwnerThread();
    if (!channel_)
        return std::unexpected(sessionError(ErrorCode::InvalidArg, "Render session is not ready."));
    auto submitted = channel_->submitFrame(std::move(submission));
    if (!submitted) {
        const Error failure = channel_->failureSnapshot().value_or(submitted.error());
        failExecution(failure);
        return std::unexpected(failure);
    }
    return {};
}

Result<RenderCaptureResult> RenderSession::capture(RenderFrameSubmission submission, const RenderCaptureDesc& desc) {
    assertOwnerThread();
    if (!channel_)
        return std::unexpected(sessionError(ErrorCode::InvalidArg, "Render session is not ready."));
    if (!channel_->isReady()) {
        const Error failure = channel_->failureSnapshot().value_or(
                sessionError(ErrorCode::Internal, "Render channel stopped before capture submission."));
        failExecution(failure);
        return std::unexpected(failure);
    }
    auto result = channel_->capture(std::move(submission), desc);
    if (!result && (isDeviceFatalError(result.error()) || !channel_->isReady()))
        failExecution(result.error());
    return result;
}

RenderSurfaceState RenderSession::resize(int width, int height) {
    assertOwnerThread();
    if (!channel_)
        return {};
    auto resized = channel_->resize(width, height);
    if (resized)
        return *resized;
    const RenderSurfaceState fallback = channel_->presentSurfaceState();
    LOG_ERROR("[RenderSession] Surface resize failed: {}", resized.error().message);
    failExecution(resized.error());
    return fallback;
}

void RenderSession::enableIBL(const std::string& hdrPath) {
    assertOwnerThread();
    if (channel_)
        channel_->enableIBL(hdrPath);
}

ResultVoid RenderSession::clearPersistentResources() {
    assertOwnerThread();
    if (!channel_)
        return {};
    auto cleared = channel_->clearAssetResources();
    if (!cleared)
        failExecution(cleared.error());
    return cleared;
}

RenderSurfaceState RenderSession::surfaceState() const {
    assertOwnerThread();
    return channel_ ? channel_->presentSurfaceState() : RenderSurfaceState{};
}

void RenderSession::assertOwnerThread() const {
    assert(owner_thread_ == std::this_thread::get_id() && "RenderSession must be accessed by its owner thread.");
}

void RenderSession::failExecution(const Error& error) {
    last_runtime_failure_ = error;
    LOG_CRITICAL("[RenderSession] Render channel failed and will be discarded: {}", error.message);
    discardChannel();
}

void RenderSession::discardChannel() {
    if (channel_) {
        channel_->shutdown();
        channel_.reset();
    }
}

}  // namespace mulan::engine
