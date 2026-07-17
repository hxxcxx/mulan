#include "runtime/detail/render_session.h"

#include "runtime/detail/render_worker.h"

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

ResultVoid RenderSession::initWindow(const ViewConfig& config, int width, int height) {
    MULAN_PROFILE_ZONE();

    assertOwnerThread();
    if (isInitialized()) {
        return {};
    }
    if (worker_) {
        discardExecutionDomain();
    }

    auto candidate = std::make_unique<RenderWorker>();
    auto initialized = candidate->initWindow(config, width, height);
    if (!initialized) {
        return initialized;
    }
    worker_ = std::move(candidate);
    submission_builder_.invalidateResources();
    last_runtime_failure_.reset();
    return {};
}

void RenderSession::shutdown() {
    assertOwnerThread();
    if (worker_) {
        worker_->shutdown();
        worker_.reset();
    }
    submission_builder_.reset();
    asset_source_ = nullptr;
    last_runtime_failure_.reset();
}

bool RenderSession::isInitialized() const {
    assertOwnerThread();
    return worker_ && worker_->isInitialized();
}

std::optional<Error> RenderSession::runtimeFailure() const {
    assertOwnerThread();
    if (last_runtime_failure_) {
        return last_runtime_failure_;
    }
    if (worker_) {
        return worker_->failureSnapshot();
    }
    return std::nullopt;
}

ResultVoid RenderSession::pollRuntime() {
    assertOwnerThread();
    if (last_runtime_failure_) {
        return std::unexpected(*last_runtime_failure_);
    }
    if (!worker_) {
        return {};
    }

    auto drained = drainWorkerEvents();
    if (!drained) {
        return drained;
    }
    if (!worker_) {
        return {};
    }
    if (worker_->isInitialized()) {
        return {};
    }

    const std::optional<Error> snapshot = worker_->failureSnapshot();
    const Error failure =
            snapshot ? *snapshot
                     : sessionError(ErrorCode::Internal, "Render worker stopped without a completion event.");
    failExecution(failure);
    return std::unexpected(failure);
}

void RenderSession::setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    assertOwnerThread();
    if (worker_ && !pollRuntime()) {
        // pollRuntime 已记录并销毁失败执行域；CPU 场景仍可更新，供下次初始化恢复。
    }
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
    if (!worker_) {
        return;
    }
    if (!pollRuntime() || !worker_) {
        return;
    }

    RenderSubmission submission = submission_builder_.build(viewState);

    // Worker 在同一锁内把 prepare 拆到可靠队列，并给 latest-frame 记录资源依赖；
    // owner 此处只提交，不等待 GPU 上传。
    auto submitted = worker_->submitFrame(std::move(submission));
    if (!submitted) {
        const std::optional<Error> snapshot = worker_->failureSnapshot();
        const Error failure = snapshot ? *snapshot : submitted.error();
        LOG_ERROR("[RenderSession] Frame submission failed: {}", failure.message);
        failExecution(failure);
    }
}

Result<engine::RenderCaptureResult> RenderSession::capture(const ViewState& viewState,
                                                           const engine::RenderCaptureDesc& desc) {
    assertOwnerThread();
    if (!worker_) {
        return std::unexpected(sessionError(ErrorCode::InvalidArg, "Render session is not initialized."));
    }
    auto polled = pollRuntime();
    if (!polled) {
        return std::unexpected(polled.error());
    }
    if (!worker_ || !worker_->isInitialized()) {
        const std::optional<Error> snapshot = worker_ ? worker_->failureSnapshot() : std::nullopt;
        const Error failure =
                snapshot ? *snapshot
                         : sessionError(ErrorCode::Internal, "Render worker stopped before capture submission.");
        failExecution(failure);
        return std::unexpected(failure);
    }
    RenderSubmission submission = submission_builder_.build(viewState);

    // capture 自身是同步 API，但其资源上传仍由 worker 可靠队列先行执行；owner
    // 不再单独等待 prepare future，完成后统一 drain ACK。
    Result<engine::RenderCaptureResult> result = worker_->capture(std::move(submission), desc);
    auto drained = drainWorkerEvents();
    if (!drained) {
        return std::unexpected(drained.error());
    }
    if (!result && (engine::isDeviceFatalError(result.error()) || (worker_ && !worker_->isInitialized()))) {
        failExecution(result.error());
    }
    return result;
}

RenderSurfaceState RenderSession::resize(int width, int height) {
    assertOwnerThread();
    if (!worker_) {
        return {};
    }
    if (!pollRuntime() || !worker_) {
        return {};
    }
    auto resized = worker_->resize(width, height);
    if (!resized) {
        LOG_ERROR("[RenderSession] Surface resize failed: {}", resized.error().message);
        const RenderSurfaceState fallback = worker_->surfaceState();
        // resize 控制任务被定义为 fatal；任何失败都可能留下半失效表面，
        // 必须立即销毁执行域，不能只在 DeviceLost 错误码时处理。
        failExecution(resized.error());
        return fallback;
    }
    return *resized;
}

void RenderSession::enableIBL(const std::string& hdrPath) {
    assertOwnerThread();
    if (!worker_) {
        return;
    }
    if (!pollRuntime() || !worker_) {
        return;
    }
    worker_->enableIBL(hdrPath);
}

RenderSurfaceState RenderSession::surfaceState() const {
    assertOwnerThread();
    return worker_ ? worker_->surfaceState() : RenderSurfaceState{};
}

void RenderSession::assertOwnerThread() const {
    assert(owner_thread_ == std::this_thread::get_id() &&
           "RenderSession must only be accessed from its owning thread.");
}

ResultVoid RenderSession::drainWorkerEvents() {
    if (!worker_) {
        return {};
    }

    std::optional<Error> failure;
    for (RenderWorkerEvent& event : worker_->drainEvents()) {
        if (event.type == RenderWorkerEventType::ResourceBatchCompleted) {
            // Builder 只接受当前 pending batch；迟到 ACK 会被安全忽略。
            submission_builder_.acknowledgeResources(event.resourceBatchId);
            continue;
        }
        if (event.type == RenderWorkerEventType::Failure && !failure) {
            failure = event.error;
        }
    }

    if (!failure) {
        return {};
    }

    const Error error = *failure;
    failExecution(error);
    return std::unexpected(error);
}

void RenderSession::failExecution(const Error& error) {
    last_runtime_failure_ = error;
    LOG_CRITICAL("[RenderSession] Execution domain failed and will be discarded: {}", error.message);
    discardExecutionDomain();
}

void RenderSession::discardExecutionDomain() {
    if (worker_) {
        worker_->shutdown();
        worker_.reset();
    }
    // 新执行域拥有空 GPU registry，下一次初始化必须从当前 CPU 场景完整恢复。
    submission_builder_.invalidateResources();
}

void RenderSession::clearAssetResources() {
    if (!worker_) {
        return;
    }
    if (!pollRuntime() || !worker_) {
        return;
    }
    auto cleared = worker_->clearAssetResources();
    if (!cleared) {
        LOG_ERROR("[RenderSession] Asset resource clearing failed: {}", cleared.error().message);
        failExecution(cleared.error());
    }
}

}  // namespace mulan::view::detail
