/**
 * @file render_thread.h
 * @brief RenderThread 在兼容 Device 范围内统一调度多个渲染表面
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 每个通道保留独立的可靠控制队列、资源状态和 latest-frame 邮箱；一个 RenderThread
 * 只使用一条 GPU 线程，以轮转方式公平执行各 Surface。OpenGL 上下文不共享线程。
 */

#pragma once

#include "render_surface_state.h"
#include "render_channel_state.h"

#include "../../scene_sync/render_submission.h"

#include <mulan/core/result/error.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/view/core/view_config.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mulan::view::detail {

class RenderExecutor;
class RenderDeviceContext;

using RenderChannelId = uint64_t;
using RenderChannelEventCallback = std::function<void()>;

enum class RenderThreadState : uint8_t {
    Healthy,
    Failed,
    Stopped,
};

struct RenderThreadStats {
    uint64_t threadId = 0;
    size_t channelCount = 0;
    size_t executedControlCount = 0;
    size_t executedFrameCount = 0;
    size_t rejectedWorkCount = 0;
    size_t failureBroadcastCount = 0;
    size_t pendingControlCount = 0;
    size_t pendingFrameCount = 0;
    RenderThreadState state = RenderThreadState::Stopped;
};

/// 轮转游标只负责公平起点；调度器仍会跳过当前无任务的通道。
class FairChannelCursor {
public:
    size_t start(size_t count) const { return count == 0 ? 0 : next_ % count; }
    void selected(size_t index, size_t count) { next_ = count == 0 ? 0 : (index + 1) % count; }
    void clamp(size_t count) { next_ = count == 0 ? 0 : next_ % count; }

private:
    size_t next_ = 0;
};

class RenderThread {
public:
    static Result<std::shared_ptr<RenderThread>> acquire(const ViewConfig& config);
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    Result<RenderChannelId> attachWindow(const ViewConfig& config, int width, int height,
                                         RenderChannelEventCallback eventCallback);
    void detach(RenderChannelId channel);

    bool isReady(RenderChannelId channel) const;
    ResultVoid submitFrame(RenderChannelId channel, RenderSubmission submission);
    Result<engine::RenderCaptureResult> capture(RenderChannelId channel, RenderSubmission submission,
                                                engine::RenderCaptureDesc desc);
    Result<RenderSurfaceState> resize(RenderChannelId channel, int width, int height);
    void enableIBL(RenderChannelId channel, std::string hdrPath);
    ResultVoid clearAssetResources(RenderChannelId channel);

    std::optional<uint64_t> takeCompletedResourceBatch(RenderChannelId channel);
    std::optional<Error> failureSnapshot(RenderChannelId channel) const;
    RenderSurfaceState surfaceState(RenderChannelId channel) const;
    RenderThreadStats stats() const;
    RenderThreadState state() const;

    /// 无真实 GPU 的状态机测试入口；仅供测试代码显式调用。
    void injectFailureForTesting(Error error);

private:
    struct Channel;
    struct ControlTask;
    using Initializer = std::function<ResultVoid(RenderExecutor&)>;

    explicit RenderThread(const ViewConfig& config);

    Result<RenderChannelId> attach(Initializer initialize, RenderChannelEventCallback eventCallback);
    Result<std::reference_wrapper<RenderDeviceContext>> ensureDeviceContext();
    void run(std::stop_token stopToken);
    std::shared_ptr<Channel> selectReadyChannelLocked(bool& hasControl, ControlTask& control,
                                                      std::optional<RenderSubmission>& frame);
    bool hasWorkLocked() const;
    bool channelHasWorkLocked(const Channel& channel) const;
    void assertInvariantsLocked() const;
    bool enqueue(RenderChannelId channel, ControlTask task);
    ResultVoid enqueueSubmissionResourcesLocked(Channel& channel, RenderSubmission& submission);
    void publishSurfaceStateLocked(Channel& channel);
    void failChannel(const std::shared_ptr<Channel>& channel, const Error& error);
    void failThread(const Error& error);
    static Error threadError(ErrorCode code, std::string_view message);

    ViewConfig config_;
    std::unique_ptr<RenderDeviceContext> device_context_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::unordered_map<RenderChannelId, std::shared_ptr<Channel>> channels_;
    std::vector<RenderChannelId> channel_order_;
    FairChannelCursor cursor_;
    RenderChannelId next_channel_ = 1;
    uint64_t thread_id_ = 0;
    size_t executed_control_count_ = 0;
    size_t executed_frame_count_ = 0;
    size_t rejected_work_count_ = 0;
    size_t failure_broadcast_count_ = 0;
    RenderThreadState state_ = RenderThreadState::Stopped;
    std::optional<Error> thread_failure_;
    bool stopping_ = false;
    // 必须最后声明并在构造函数体启动，禁止线程观察到尚未构造的状态成员。
    std::jthread thread_;
};

}  // namespace mulan::view::detail
