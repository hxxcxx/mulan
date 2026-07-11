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
    core::Result<void> initOffscreen(int width, int height);
    void shutdown();
    bool isInitialized() const { return initialized_.load(); }

    void submitFrame(RenderSubmission submission);
    void resize(int width, int height);
    void enableIBL(std::string hdrPath);
    void clearAssetResources();
    bool readbackPixels(std::vector<uint8_t>& pixels);
    bool configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    bool configureOffscreenSurface(const RenderSurfaceDesc& desc);

    bool isOffscreenSurface() const;
    uint32_t surfaceWidth() const;
    uint32_t surfaceHeight() const;
    uint64_t surfaceGeneration() const;

private:
    struct ControlTask {
        std::function<void(RenderRuntime&)> execute;
        bool flushFrame = false;
    };

    core::Result<void> start(std::function<core::Result<void>(RenderRuntime&)> initialize);
    void run(std::stop_token stopToken, std::function<core::Result<void>(RenderRuntime&)> initialize,
             std::promise<core::Result<void>> ready);
    void enqueue(ControlTask task);
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
    bool offscreen_ = false;
    uint64_t surface_generation_ = 0;
};

}  // namespace mulan::view
