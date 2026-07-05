/**
 * @file render_thread.h
 * @brief RenderThread 管理渲染任务队列和专用执行线程的生命周期。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_queue.h"

#include <atomic>
#include <expected>
#include <future>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <mulan/core/result/error.h>

namespace mulan::view {

class RenderThread {
public:
    RenderThread() = default;
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    core::Result<void> start(std::string name = "RenderThread");
    core::Result<void> requestShutdown();
    void stop();

    bool running() const { return running_.load(); }
    RenderQueue& queue() { return queue_; }
    const RenderQueue& queue() const { return queue_; }

    template <class Fn>
    auto submit(RenderTaskKind kind, std::string label, Fn&& fn)
            -> core::Result<std::future<std::invoke_result_t<Fn&>>> {
        return queue_.submit(kind, std::move(label), std::forward<Fn>(fn));
    }

private:
    void run(std::stop_token stopToken);

    RenderQueue queue_;
    std::jthread worker_;
    std::atomic_bool running_ = false;
};

}  // namespace mulan::view
