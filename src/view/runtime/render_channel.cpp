/**
 * @file render_channel.cpp
 * @brief RenderChannel 到共享 RenderThread 通道协议的转发实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "detail/render_channel.h"

#include <mulan/core/profiling/profile.h>

#include <utility>

namespace mulan::view::detail {

RenderChannel::~RenderChannel() {
    shutdown();
}

ResultVoid RenderChannel::init(const ViewConfig& config, int width, int height,
                               RenderChannelEventCallback eventCallback) {
    MULAN_PROFILE_ZONE();

    if (isReady()) {
        return {};
    }
    if (thread_ || channel_ != 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render channel is already attached."));
    }
    const RenderDeviceConfig deviceConfig = RenderDeviceConfig::fromView(config);
    const PresentSurfaceConfig presentConfig = PresentSurfaceConfig::fromView(config);
    auto thread = RenderThread::acquire(deviceConfig);
    if (!thread) {
        return std::unexpected(thread.error());
    }
    auto channel = (*thread)->attachChannel(presentConfig, width, height, std::move(eventCallback));
    if (!channel) {
        return std::unexpected(channel.error());
    }
    thread_ = std::move(*thread);
    channel_ = *channel;
    return {};
}

void RenderChannel::shutdown() {
    if (thread_ && channel_ != 0) {
        thread_->detach(channel_);
    }
    channel_ = 0;
    thread_.reset();
}

bool RenderChannel::isReady() const {
    return thread_ && channel_ != 0 && thread_->isReady(channel_);
}

ResultVoid RenderChannel::submitFrame(RenderSubmission submission) {
    if (!thread_ || channel_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render channel is not ready."));
    }
    return thread_->submitFrame(channel_, std::move(submission));
}

Result<engine::RenderCaptureResult> RenderChannel::capture(RenderSubmission submission,
                                                           engine::RenderCaptureDesc desc) {
    if (!thread_ || channel_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render channel is not ready."));
    }
    return thread_->capture(channel_, std::move(submission), desc);
}

Result<PresentSurfaceState> RenderChannel::resize(int width, int height) {
    if (!thread_ || channel_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render channel is not ready."));
    }
    return thread_->resize(channel_, width, height);
}

void RenderChannel::enableIBL(std::string hdrPath) {
    if (thread_ && channel_ != 0) {
        thread_->enableIBL(channel_, std::move(hdrPath));
    }
}

ResultVoid RenderChannel::clearAssetResources() {
    if (!thread_ || channel_ == 0) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Render channel is not ready."));
    }
    return thread_->clearAssetResources(channel_);
}

std::optional<uint64_t> RenderChannel::takeCompletedResourceBatch() {
    return thread_ && channel_ != 0 ? thread_->takeCompletedResourceBatch(channel_) : std::nullopt;
}

std::optional<Error> RenderChannel::failureSnapshot() const {
    return thread_ && channel_ != 0 ? thread_->failureSnapshot(channel_) : std::nullopt;
}

PresentSurfaceState RenderChannel::presentSurfaceState() const {
    return thread_ && channel_ != 0 ? thread_->presentSurfaceState(channel_) : PresentSurfaceState{};
}

}  // namespace mulan::view::detail
