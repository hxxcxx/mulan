/**
 * @file render_channel.h
 * @brief RenderChannel 是 RenderSession 接入共享 RenderThread 的独占通道外观
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include "render_thread.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mulan::engine::detail {

class RenderChannel {
public:
    RenderChannel() = default;
    ~RenderChannel();

    RenderChannel(const RenderChannel&) = delete;
    RenderChannel& operator=(const RenderChannel&) = delete;

    ResultVoid init(const RenderSessionConfig& config, int width, int height, RenderChannelEventCallback eventCallback);
    void shutdown();

    bool isReady() const;
    ResultVoid submitFrame(RenderFrameSubmission submission);
    Result<engine::RenderCaptureResult> capture(RenderFrameSubmission submission, engine::RenderCaptureDesc desc);
    Result<RenderSurfaceState> resize(int width, int height);
    void enableIBL(std::string hdrPath);
    ResultVoid clearAssetResources();

    std::optional<uint64_t> takeCompletedResourceBatch();
    std::optional<Error> failureSnapshot() const;
    RenderSurfaceState presentSurfaceState() const;

private:
    std::shared_ptr<RenderThread> thread_;
    RenderChannelId channel_ = 0;
};

}  // namespace mulan::engine::detail
