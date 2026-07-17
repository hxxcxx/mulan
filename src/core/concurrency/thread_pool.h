/**
 * @file thread_pool.h
 * @brief 固定大小的通用 CPU 工作线程池。
 * @author hxxcxx
 * @date 2026-07-17
 *
 * ThreadPool 只提供叶子任务的异步执行与结果传递，不承担任务优先级、依赖图、
 * 动态扩缩容或强制取消。成功提交的任务会在正常析构期间执行完毕。
 */
#pragma once

#include "../core_export.h"

#include <concepts>
#include <cstddef>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace mulan::core {

class CORE_API ThreadPool final {
public:
    explicit ThreadPool(size_t workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    [[nodiscard]] size_t workerCount() const noexcept;

    /**
     * @brief 提交一个无参数任务并返回其异步结果。
     *
     * 任务抛出的异常由 std::future 保存并在 get() 时重新抛出。调用方必须保证
     * 任务捕获的引用在 future 完成前持续有效，且任务不得同步等待同一线程池
     * 中新提交的子任务。
     */
    template <class Function>
        requires std::invocable<std::decay_t<Function>&>
    [[nodiscard]] auto submit(Function&& function) -> std::future<std::invoke_result_t<std::decay_t<Function>&>> {
        using Callable = std::decay_t<Function>;
        using Return = std::invoke_result_t<Callable&>;

        std::packaged_task<Return()> typedTask{ std::forward<Function>(function) };
        auto result = typedTask.get_future();
        enqueue(Task{ [task = std::move(typedTask)]() mutable { task(); } });
        return result;
    }

private:
    using Task = std::packaged_task<void()>;

    void enqueue(Task task);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mulan::core
