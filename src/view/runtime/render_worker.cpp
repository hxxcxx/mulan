/**
 * @file render_worker.cpp
 * @brief RenderWorker 到设备级 GpuExecutionDomain 客户端协议的转发实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "runtime/detail/render_worker.h"

namespace mulan::view::detail {

RenderWorker::~RenderWorker() {
    shutdown();
}

ResultVoid RenderWorker::initWindow(const ViewConfig& config, int width, int height) {
    if (isInitialized()) {
        return {};
    }
    if (domain_ || client_ != 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render worker already owns a domain client."));
    }
    auto domain = GpuExecutionDomain::acquire(config);
    if (!domain) {
        return std::unexpected(domain.error());
    }
    auto client = (*domain)->attachWindow(config, width, height);
    if (!client) {
        return std::unexpected(client.error());
    }
    domain_ = std::move(*domain);
    client_ = *client;
    return {};
}

void RenderWorker::shutdown() {
    if (domain_ && client_ != 0) {
        domain_->detach(client_);
    }
    client_ = 0;
    domain_.reset();
}

bool RenderWorker::isInitialized() const {
    return domain_ && client_ != 0 && domain_->isReady(client_);
}

ResultVoid RenderWorker::submitFrame(RenderSubmission submission) {
    if (!domain_ || client_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render worker is not initialized."));
    }
    return domain_->submitFrame(client_, std::move(submission));
}

Result<engine::RenderCaptureResult> RenderWorker::capture(RenderSubmission submission, engine::RenderCaptureDesc desc) {
    if (!domain_ || client_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render worker is not initialized."));
    }
    return domain_->capture(client_, std::move(submission), desc);
}

Result<RenderSurfaceState> RenderWorker::resize(int width, int height) {
    if (!domain_ || client_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render worker is not initialized."));
    }
    return domain_->resize(client_, width, height);
}

void RenderWorker::enableIBL(std::string hdrPath) {
    if (domain_ && client_ != 0) {
        domain_->enableIBL(client_, std::move(hdrPath));
    }
}

ResultVoid RenderWorker::clearAssetResources() {
    if (!domain_ || client_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render worker is not initialized."));
    }
    return domain_->clearAssetResources(client_);
}

std::vector<RenderWorkerEvent> RenderWorker::drainEvents() {
    return domain_ && client_ != 0 ? domain_->drainEvents(client_) : std::vector<RenderWorkerEvent>{};
}

std::optional<Error> RenderWorker::failureSnapshot() const {
    return domain_ && client_ != 0 ? domain_->failureSnapshot(client_) : std::nullopt;
}

RenderSurfaceState RenderWorker::surfaceState() const {
    return domain_ && client_ != 0 ? domain_->surfaceState(client_) : RenderSurfaceState{};
}

}  // namespace mulan::view::detail
