/**
 * @file render_session.h
 * @brief RenderSession 是 owner 线程访问渲染执行域的唯一公开会话。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include "render_config.h"
#include "render_surface_state.h"
#include "../frontend/render_capture.h"
#include "../frontend/render_frame_submission.h"

#include <mulan/core/result/error.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace mulan::engine {
namespace detail {
class RenderChannel;
}

class RenderSession {
public:
    RenderSession();
    ~RenderSession();

    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;

    ResultVoid init(const RenderSessionConfig& config, int width, int height,
                    std::function<void()> runtimeEventCallback);
    void shutdown();

    bool isReady() const;
    Result<std::optional<uint64_t>> consumeRuntimeEvents();
    ResultVoid submitFrame(RenderFrameSubmission submission);
    Result<RenderCaptureResult> capture(RenderFrameSubmission submission, const RenderCaptureDesc& desc);
    RenderSurfaceState resize(int width, int height);
    void enableIBL(const std::string& hdrPath);
    ResultVoid clearPersistentResources();
    RenderSurfaceState surfaceState() const;

private:
    void assertOwnerThread() const;
    void failExecution(const Error& error);
    void discardChannel();

    std::unique_ptr<detail::RenderChannel> channel_;
    std::optional<Error> last_runtime_failure_;
    std::thread::id owner_thread_;
};

}  // namespace mulan::engine
