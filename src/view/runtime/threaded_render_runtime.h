/**
 * @file threaded_render_runtime.h
 * @brief ThreadedRenderRuntime 以控制队列和最新帧邮箱驱动专用渲染线程。
 */
#pragma once

#include <mulan/view/runtime/render_runtime.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace mulan::view {

class ThreadedRenderRuntime {
public:
    ThreadedRenderRuntime() = default;
    ~ThreadedRenderRuntime();
    ThreadedRenderRuntime(const ThreadedRenderRuntime&) = delete;
    ThreadedRenderRuntime& operator=(const ThreadedRenderRuntime&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height);
    core::Result<void> initOffscreen(const ViewConfig& config, int width, int height);
    core::Result<void> initOffscreen(int width, int height);
    void shutdown();
    bool isInitialized() const { return initialized_.load(); }

    void submitFrame(RenderSubmission submission);
    core::Result<engine::RenderCaptureResult> capture(RenderSubmission submission, engine::RenderCaptureDesc desc);
    void resize(int width, int height);
    void enableIBL(std::string hdrPath);
    void clearAssetResources();

    uint32_t surfaceWidth() const;
    uint32_t surfaceHeight() const;
    uint64_t surfaceGeneration() const;

private:
    struct ControlTask {
        std::function<void(RenderRuntime&)> execute;
        bool flushFrame = false;
        // 队列请求可能晚于提交它的视图销毁；关闭时丢弃请求也必须唤醒调用方。
        std::function<void()> cancel;
    };

    core::Result<void> start(std::function<core::Result<void>(RenderRuntime&)> initialize);
    void run(std::stop_token stopToken, std::function<core::Result<void>(RenderRuntime&)> initialize,
             std::promise<core::Result<void>> ready);
    bool enqueue(ControlTask task);
    void renderLatest(RenderRuntime& runtime);
    void publishSurfaceState(const RenderRuntime& runtime);

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<ControlTask> controls_;
    std::optional<RenderSubmission> latest_frame_;
    std::jthread worker_;
    std::atomic_bool initialized_ = false;
    bool closing_ = false;
    uint32_t surface_width_ = 0;
    uint32_t surface_height_ = 0;
    uint64_t surface_generation_ = 0;
};

}  // namespace mulan::view
