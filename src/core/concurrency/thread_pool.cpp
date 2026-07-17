/**
 * @file thread_pool.cpp
 * @brief 固定大小 CPU 工作线程池实现。
 * @author hxxcxx
 * @date 2026-07-17
 */

#include "thread_pool.h"

#include <mulan/core/profiling/profile.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mulan::core {

struct ThreadPool::Impl {
    explicit Impl(size_t workerCount) {
        if (workerCount == 0)
            throw std::invalid_argument("ThreadPool requires at least one worker.");

        workers.reserve(workerCount);
        try {
            for (size_t index = 0; index < workerCount; ++index) {
                workers.emplace_back([this, index](std::stop_token stopToken) { workerLoop(index, stopToken); });
            }
        } catch (...) {
            // 构造尚未完成，不可能存在外部提交的任务；这里只需安全停止已创建的线程。
            for (auto& worker : workers)
                worker.request_stop();
            wake.notify_all();
            throw;
        }
    }

    ~Impl() {
        {
            std::scoped_lock lock(mutex);
            accepting = false;
        }
        wake.notify_all();

        // 正常关闭不请求 stop：先排空所有已接受任务，再显式 join，避免 jthread
        // 析构时的自动 request_stop 让任务静默丢失。
        for (auto& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
    }

    void workerLoop(size_t index, std::stop_token stopToken) {
        [[maybe_unused]] const std::string threadName = "WorkerPool/" + std::to_string(index);
        MULAN_PROFILE_THREAD(threadName.c_str());

        for (;;) {
            Task task;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, stopToken, [this] { return !accepting || !tasks.empty(); });

                // stopToken 只用于构造失败时的回滚。正常析构通过 accepting=false
                // 进入排空路径，不会丢弃已经接受的任务。
                if (stopToken.stop_requested())
                    return;

                if (tasks.empty()) {
                    if (!accepting)
                        return;
                    continue;
                }

                task = std::move(tasks.front());
                tasks.pop_front();
            }
            task();
        }
    }

    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Task> tasks;
    bool accepting = true;

    // 必须最后声明：线程须先于其访问的队列和同步状态销毁。
    std::vector<std::jthread> workers;
};

ThreadPool::ThreadPool(size_t workerCount) : impl_(std::make_unique<Impl>(workerCount)) {
}

ThreadPool::~ThreadPool() = default;

size_t ThreadPool::workerCount() const noexcept {
    return impl_->workers.size();
}

void ThreadPool::enqueue(Task task) {
    {
        std::scoped_lock lock(impl_->mutex);
        if (!impl_->accepting)
            throw std::logic_error("Cannot submit a task to a stopping ThreadPool.");
        impl_->tasks.push_back(std::move(task));
    }
    impl_->wake.notify_one();
}

}  // namespace mulan::core
