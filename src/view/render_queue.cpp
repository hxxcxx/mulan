#include "render_queue.h"

namespace mulan::view {

RenderQueue::~RenderQueue() {
    close();
}

core::Result<void> RenderQueue::submit(RenderTask task) {
    if (!task) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "Cannot submit an empty render task."));
    }

    {
        std::scoped_lock lock(mutex_);
        if (closed_) {
            return std::unexpected(
                    core::Error::make(core::ErrorCode::InvalidArg, "Cannot submit to a closed render queue."));
        }
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
    return {};
}

std::optional<RenderTask> RenderQueue::waitPop(std::stop_token stopToken) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, stopToken, [this] { return closed_ || !tasks_.empty(); });

    if (tasks_.empty()) {
        return std::nullopt;
    }

    auto task = std::move(tasks_.front());
    tasks_.pop_front();
    return task;
}

void RenderQueue::close() {
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}

bool RenderQueue::closed() const {
    std::scoped_lock lock(mutex_);
    return closed_;
}

std::size_t RenderQueue::pendingCount() const {
    std::scoped_lock lock(mutex_);
    return tasks_.size();
}

}  // namespace mulan::view
