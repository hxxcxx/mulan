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
#include "render_runtime_config.h"

#include "../../scene_sync/render_submission.h"

#include <mulan/core/result/error.h>
#include <mulan/render/frontend/render_capture.h>

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
#include <vector>

namespace mulan::view::detail {

class RenderExecutor;
class RenderDeviceContext;
class RenderChannel;

using RenderChannelId = uint64_t;
using RenderChannelEventCallback = std::function<void()>;

class RenderThread {
public:
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

private:
    friend class RenderChannel;

    static Result<std::shared_ptr<RenderThread>> acquire(const RenderDeviceConfig& config);

    Result<RenderChannelId> attachChannel(const RenderSurfaceConfig& config, int width, int height,
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

    enum class State : uint8_t {
        Healthy,
        Failed,
        Stopped,
    };

    struct Channel;
    struct ControlTask;

    explicit RenderThread(const RenderDeviceConfig& config);

    ResultVoid initializeChannel(const RenderSurfaceConfig& config, int width, int height, Channel& channel);
    ResultVoid ensureDeviceContext();
    void run(std::stop_token stopToken);
    Channel* findChannelLocked(RenderChannelId channel);
    const Channel* findChannelLocked(RenderChannelId channel) const;
    Channel* selectReadyChannelLocked(std::optional<ControlTask>& control, std::optional<RenderSubmission>& frame);
    bool hasWorkLocked() const;
    bool channelHasWorkLocked(const Channel& channel) const;
    bool isHealthy() const;
    bool enqueue(RenderChannelId channel, ControlTask task);
    ResultVoid enqueueSubmissionResourcesLocked(Channel& channel, RenderSubmission& submission);
    void publishSurfaceStateLocked(Channel& channel);
    void executeFrame(Channel& channel, RenderSubmission submission);
    void executeControl(Channel& channel, ControlTask control);
    static void finishControlTask(ControlTask& control, ResultVoid result) noexcept;
    void failChannel(Channel& channel, const Error& error);
    void failThread(const Error& error);
    static Error threadError(ErrorCode code, std::string_view message);

    RenderDeviceConfig config_;
    std::unique_ptr<RenderDeviceContext> device_context_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<std::unique_ptr<Channel>> channels_;
    size_t next_channel_index_ = 0;
    RenderChannelId next_channel_ = 1;
    uint64_t thread_id_ = 0;
    State state_ = State::Stopped;
    std::optional<Error> thread_failure_;
    bool stopping_ = false;
    // 必须最后声明并在构造函数体启动，禁止线程观察到尚未构造的状态成员。
    std::jthread thread_;
};

}  // namespace mulan::view::detail
