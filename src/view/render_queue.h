/**
 * @file render_queue.h
 * @brief RenderQueue 提供跨线程提交渲染任务的有界入口。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_task.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

#include <mulan/core/result/error.h>

namespace mulan::view {

class RenderQueue {
public:
    RenderQueue() = default;
    ~RenderQueue();

    RenderQueue(const RenderQueue&) = delete;
    RenderQueue& operator=(const RenderQueue&) = delete;

    core::Result<void> submit(RenderTask task);

    template <class Fn>
    auto submit(RenderTaskKind kind, std::string label, Fn&& fn)
            -> core::Result<std::future<std::invoke_result_t<Fn&>>>;

    std::optional<RenderTask> waitPop(std::stop_token stopToken);
    void close();

    bool closed() const;
    std::size_t pendingCount() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<RenderTask> tasks_;
    bool closed_ = false;
};

template <class Fn>
auto RenderQueue::submit(RenderTaskKind kind, std::string label, Fn&& fn)
        -> core::Result<std::future<std::invoke_result_t<Fn&>>> {
    using Result = std::invoke_result_t<Fn&>;

    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    auto work = [promise, fn = std::forward<Fn>(fn)]() mutable {
        try {
            if constexpr (std::is_void_v<Result>) {
                fn();
                promise->set_value();
            } else {
                promise->set_value(fn());
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    auto submitted = submit(RenderTask{ kind, std::move(label), std::move(work) });
    if (!submitted) {
        return std::unexpected(submitted.error());
    }
    return future;
}

}  // namespace mulan::view
