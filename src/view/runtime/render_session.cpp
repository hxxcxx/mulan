#include "runtime/detail/render_session.h"

#include "runtime/detail/render_executor.h"
#include "runtime/detail/render_worker.h"

#include <mulan/core/log/log.h>
#include <mulan/rhi/engine_error_code.h>

#include <cassert>
#include <string_view>
#include <thread>
#include <utility>

namespace mulan::view::detail {
namespace {

core::Error sessionError(core::ErrorCode code, std::string_view message) {
    return core::Error::make(code, message);
}

bool isGpuExecutionError(const core::Error& error) {
    return error.code >= static_cast<int32_t>(engine::EngineErrorCode::DeviceLost) && error.code < 2000;
}

}  // namespace

RenderSession::RenderSession() : owner_thread_(std::this_thread::get_id()) {
}

RenderSession::~RenderSession() {
    shutdown();
}

core::Result<void> RenderSession::initWindow(const ViewConfig& config, int width, int height) {
    assertOwnerThread();
    if (isInitialized()) {
        return {};
    }
    if (execution_mode_ != ExecutionMode::None) {
        discardExecutionDomain();
    }

    if (config.executionMode == RenderExecutionMode::Threaded) {
        auto candidate = std::make_unique<RenderWorker>();
        auto initialized = candidate->initWindow(config, width, height);
        if (!initialized) {
            return initialized;
        }
        worker_ = std::move(candidate);
        execution_mode_ = ExecutionMode::Threaded;
    } else {
        auto candidate = std::make_unique<RenderExecutor>();
        auto initialized = candidate->initWindow(config, width, height);
        if (!initialized) {
            return initialized;
        }
        inline_executor_ = std::move(candidate);
        execution_mode_ = ExecutionMode::Inline;
    }
    submission_builder_.invalidateResources();
    return {};
}

core::Result<void> RenderSession::initOffscreen(const ViewConfig& config, int width, int height) {
    assertOwnerThread();
    if (isInitialized()) {
        return {};
    }
    if (execution_mode_ != ExecutionMode::None) {
        discardExecutionDomain();
    }

    if (config.executionMode == RenderExecutionMode::Threaded) {
        auto candidate = std::make_unique<RenderWorker>();
        auto initialized = candidate->initOffscreen(config, width, height);
        if (!initialized) {
            return initialized;
        }
        worker_ = std::move(candidate);
        execution_mode_ = ExecutionMode::Threaded;
    } else {
        auto candidate = std::make_unique<RenderExecutor>();
        auto initialized = candidate->initOffscreen(config, width, height);
        if (!initialized) {
            return initialized;
        }
        inline_executor_ = std::move(candidate);
        execution_mode_ = ExecutionMode::Inline;
    }
    submission_builder_.invalidateResources();
    return {};
}

void RenderSession::shutdown() {
    assertOwnerThread();
    if (worker_) {
        worker_->shutdown();
        worker_.reset();
    }
    if (inline_executor_) {
        inline_executor_->shutdown();
        inline_executor_.reset();
    }
    submission_builder_.reset();
    asset_source_ = nullptr;
    execution_mode_ = ExecutionMode::None;
}

bool RenderSession::isInitialized() const {
    assertOwnerThread();
    switch (execution_mode_) {
    case ExecutionMode::Inline: return inline_executor_ && inline_executor_->isInitialized();
    case ExecutionMode::Threaded: return worker_ && worker_->isInitialized();
    case ExecutionMode::None: return false;
    }
    return false;
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
    if (!isInitialized()) {
        return;
    }

    RenderSubmission submission = submission_builder_.build(viewState);
    submission.surfaceGeneration = surfaceState().generation;
    auto prepared = prepareSubmissionResources(submission);
    if (!prepared) {
        LOG_ERROR("[RenderSession] Persistent resource preparation failed: {}", prepared.error().message);
        failExecution(prepared.error());
        return;
    }

    if (execution_mode_ == ExecutionMode::Threaded) {
        auto submitted = worker_->submitFrame(std::move(submission));
        if (!submitted) {
            LOG_ERROR("[RenderSession] Frame submission failed: {}", submitted.error().message);
            failExecution(submitted.error());
        }
    } else {
        auto rendered = inline_executor_->executeFrame(submission);
        if (!rendered) {
            LOG_ERROR("[RenderSession] Frame execution failed: {}", rendered.error().message);
            failExecution(rendered.error());
        }
    }
}

core::Result<engine::RenderCaptureResult> RenderSession::capture(const ViewState& viewState,
                                                                 const engine::RenderCaptureDesc& desc) {
    assertOwnerThread();
    if (!isInitialized()) {
        return std::unexpected(sessionError(core::ErrorCode::InvalidArg, "Render session is not initialized."));
    }

    RenderSubmission submission = submission_builder_.build(viewState);
    submission.surfaceGeneration = surfaceState().generation;
    auto prepared = prepareSubmissionResources(submission);
    if (!prepared) {
        failExecution(prepared.error());
        return std::unexpected(prepared.error());
    }

    core::Result<engine::RenderCaptureResult> result;
    if (execution_mode_ == ExecutionMode::Threaded) {
        result = worker_->capture(std::move(submission), desc);
    } else {
        result = inline_executor_->capture(submission, desc);
    }
    if (!result && (isGpuExecutionError(result.error()) ||
                    (execution_mode_ == ExecutionMode::Threaded && worker_ && !worker_->isInitialized()))) {
        failExecution(result.error());
    }
    return result;
}

RenderSurfaceState RenderSession::resize(int width, int height) {
    assertOwnerThread();
    if (execution_mode_ == ExecutionMode::Threaded && worker_) {
        auto resized = worker_->resize(width, height);
        if (!resized) {
            LOG_ERROR("[RenderSession] Surface resize failed: {}", resized.error().message);
            const RenderSurfaceState fallback = worker_->surfaceState();
            if (isGpuExecutionError(resized.error())) {
                failExecution(resized.error());
            }
            return fallback;
        }
        return *resized;
    }
    if (execution_mode_ == ExecutionMode::Inline && inline_executor_) {
        auto resized = inline_executor_->resize(width, height);
        if (!resized) {
            LOG_ERROR("[RenderSession] Surface resize failed: {}", resized.error().message);
            const RenderSurfaceState fallback = inline_executor_->surfaceState();
            if (isGpuExecutionError(resized.error())) {
                failExecution(resized.error());
            }
            return fallback;
        }
        return *resized;
    }
    return {};
}

void RenderSession::enableIBL(const std::string& hdrPath) {
    assertOwnerThread();
    if (execution_mode_ == ExecutionMode::Threaded && worker_) {
        worker_->enableIBL(hdrPath);
    } else if (execution_mode_ == ExecutionMode::Inline && inline_executor_) {
        inline_executor_->enableIBL(hdrPath);
    }
}

RenderSurfaceState RenderSession::surfaceState() const {
    assertOwnerThread();
    if (execution_mode_ == ExecutionMode::Threaded && worker_) {
        return worker_->surfaceState();
    }
    if (execution_mode_ == ExecutionMode::Inline && inline_executor_) {
        return inline_executor_->surfaceState();
    }
    return {};
}

void RenderSession::assertOwnerThread() const {
    assert(owner_thread_ == std::this_thread::get_id() &&
           "RenderSession must only be accessed from its owning thread.");
}

core::Result<void> RenderSession::prepareSubmissionResources(RenderSubmission& submission) {
    if (!submission.hasResourceUpdates()) {
        return {};
    }

    const uint64_t batchId = submission.resourceBatchId;
    core::Result<void> prepared;
    if (execution_mode_ == ExecutionMode::Threaded && worker_) {
        prepared = worker_->prepareResources(submission.prepare);
    } else if (execution_mode_ == ExecutionMode::Inline && inline_executor_) {
        prepared = inline_executor_->prepareResources(submission.prepare);
    } else {
        return std::unexpected(sessionError(core::ErrorCode::InvalidArg, "Render execution is not available."));
    }
    if (!prepared) {
        return prepared;
    }

    submission_builder_.acknowledgeResources(batchId);
    submission.prepare.clear();
    submission.resourceBatchId = 0;
    return {};
}

void RenderSession::failExecution(const core::Error& error) {
    LOG_CRITICAL("[RenderSession] Execution domain failed and will be discarded: {}", error.message);
    discardExecutionDomain();
}

void RenderSession::discardExecutionDomain() {
    if (worker_) {
        worker_->shutdown();
        worker_.reset();
    }
    if (inline_executor_) {
        inline_executor_->shutdown();
        inline_executor_.reset();
    }
    execution_mode_ = ExecutionMode::None;
    // 新执行域拥有空 GPU registry，下一次初始化必须从当前 CPU 场景完整恢复。
    submission_builder_.invalidateResources();
}

void RenderSession::clearAssetResources() {
    if (execution_mode_ == ExecutionMode::Threaded && worker_) {
        auto cleared = worker_->clearAssetResources();
        if (!cleared) {
            LOG_ERROR("[RenderSession] Asset resource clearing failed: {}", cleared.error().message);
        }
    } else if (execution_mode_ == ExecutionMode::Inline && inline_executor_) {
        inline_executor_->clearAssetResources();
    }
}

}  // namespace mulan::view::detail
