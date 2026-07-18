#include "detail/render_session.h"

#include "detail/render_channel.h"

#include <mulan/core/log/log.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/rhi/engine_error_code.h>

#include <cassert>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace mulan::view::detail {
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

ResultVoid RenderSession::initWindow(const ViewConfig& config, int width, int height,
                                     std::function<void()> runtimeEventCallback) {
    MULAN_PROFILE_ZONE();

    assertOwnerThread();
    if (isInitialized()) {
        return {};
    }
    if (channel_) {
        discardRenderChannel();
    }

    auto candidate = std::make_unique<RenderChannel>();
    auto initialized = candidate->initWindow(config, width, height, std::move(runtimeEventCallback));
    if (!initialized) {
        return initialized;
    }
    channel_ = std::move(candidate);
    submission_builder_.invalidateResources();
    last_runtime_failure_.reset();
    return {};
}

void RenderSession::shutdown() {
    assertOwnerThread();
    if (channel_) {
        channel_->shutdown();
        channel_.reset();
    }
    submission_builder_.reset();
    asset_source_ = nullptr;
    last_runtime_failure_.reset();
}

bool RenderSession::isInitialized() const {
    assertOwnerThread();
    return channel_ && channel_->isInitialized();
}

ResultVoid RenderSession::consumeRuntimeEvents() {
    assertOwnerThread();
    if (last_runtime_failure_) {
        return std::unexpected(*last_runtime_failure_);
    }
    if (!channel_) {
        return {};
    }

    auto consumed = consumeChannelState();
    if (!consumed) {
        return consumed;
    }
    if (!channel_) {
        return {};
    }
    if (channel_->isInitialized()) {
        return {};
    }

    const std::optional<Error> snapshot = channel_->failureSnapshot();
    const Error failure =
            snapshot ? *snapshot
                     : sessionError(ErrorCode::Internal, "Render channel stopped without a failure snapshot.");
    failExecution(failure);
    return std::unexpected(failure);
}

void RenderSession::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    assertOwnerThread();
    if (assets != asset_source_) {
        clearAssetResources();
    }
    asset_source_ = assets;
    submission_builder_.setScene(scene, assets);
}

void RenderSession::setPreviewLayer(const PreviewLayer* preview) {
    assertOwnerThread();
    submission_builder_.setPreviewLayer(preview);
}

void RenderSession::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    assertOwnerThread();
    submission_builder_.setLightEnvironment(lightEnvironment);
}

void RenderSession::submitFrame(const ViewState& viewState) {
    assertOwnerThread();
    if (!channel_) {
        return;
    }

    RenderSubmission submission = submission_builder_.build(viewState);

    // Channel 在同一锁内把 prepare 拆到可靠队列；控制任务始终先于 latest-frame 执行。
    auto submitted = channel_->submitFrame(std::move(submission));
    if (!submitted) {
        const std::optional<Error> snapshot = channel_->failureSnapshot();
        const Error failure = snapshot ? *snapshot : submitted.error();
        LOG_ERROR("[RenderSession] Frame submission failed: {}", failure.message);
        failExecution(failure);
    }
}

Result<engine::RenderCaptureResult> RenderSession::capture(const ViewState& viewState,
                                                           const engine::RenderCaptureDesc& desc) {
    assertOwnerThread();
    if (!channel_) {
        return std::unexpected(sessionError(ErrorCode::InvalidArg, "Render session is not initialized."));
    }
    if (!channel_->isInitialized()) {
        const std::optional<Error> snapshot = channel_->failureSnapshot();
        const Error failure =
                snapshot ? *snapshot
                         : sessionError(ErrorCode::Internal, "Render channel stopped before capture submission.");
        failExecution(failure);
        return std::unexpected(failure);
    }
    RenderSubmission submission = submission_builder_.build(viewState);

    // capture 自身是同步 API，但其资源上传仍由通道可靠队列先行执行；owner
    // 不再单独等待 prepare future，完成后统一消费 ACK。
    Result<engine::RenderCaptureResult> result = channel_->capture(std::move(submission), desc);
    auto consumed = consumeChannelState();
    if (!consumed) {
        return std::unexpected(consumed.error());
    }
    if (!result && (engine::isDeviceFatalError(result.error()) || (channel_ && !channel_->isInitialized()))) {
        failExecution(result.error());
    }
    return result;
}

RenderSurfaceState RenderSession::resize(int width, int height) {
    assertOwnerThread();
    if (!channel_) {
        return {};
    }
    auto resized = channel_->resize(width, height);
    if (!resized) {
        LOG_ERROR("[RenderSession] Surface resize failed: {}", resized.error().message);
        const RenderSurfaceState fallback = channel_->surfaceState();
        // resize 控制任务被定义为 fatal；任何失败都可能留下半失效表面，
        // 必须立即销毁渲染通道，不能只在 DeviceLost 错误码时处理。
        failExecution(resized.error());
        return fallback;
    }
    return *resized;
}

void RenderSession::enableIBL(const std::string& hdrPath) {
    assertOwnerThread();
    if (!channel_) {
        return;
    }
    channel_->enableIBL(hdrPath);
}

RenderSurfaceState RenderSession::surfaceState() const {
    assertOwnerThread();
    return channel_ ? channel_->surfaceState() : RenderSurfaceState{};
}

void RenderSession::assertOwnerThread() const {
    assert(owner_thread_ == std::this_thread::get_id() &&
           "RenderSession must only be accessed from its owning thread.");
}

ResultVoid RenderSession::consumeChannelState() {
    if (!channel_) {
        return {};
    }

    if (const auto completed = channel_->takeCompletedResourceBatch()) {
        // Builder 只接受当前 pending batch；迟到 ACK 会被安全忽略。
        submission_builder_.acknowledgeResources(*completed);
    }

    const std::optional<Error> failure = channel_->failureSnapshot();
    if (!failure) {
        return {};
    }

    const Error error = *failure;
    failExecution(error);
    return std::unexpected(error);
}

void RenderSession::failExecution(const Error& error) {
    last_runtime_failure_ = error;
    LOG_CRITICAL("[RenderSession] Render channel failed and will be discarded: {}", error.message);
    discardRenderChannel();
}

void RenderSession::discardRenderChannel() {
    if (channel_) {
        channel_->shutdown();
        channel_.reset();
    }
    // 新渲染线程拥有空 GPU registry，下一次初始化必须从当前 CPU 场景完整恢复。
    submission_builder_.invalidateResources();
}

void RenderSession::clearAssetResources() {
    if (!channel_) {
        return;
    }
    auto cleared = channel_->clearAssetResources();
    if (!cleared) {
        LOG_ERROR("[RenderSession] Asset resource clearing failed: {}", cleared.error().message);
        failExecution(cleared.error());
    }
}

}  // namespace mulan::view::detail
