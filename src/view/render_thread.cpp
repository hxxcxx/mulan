#include "render_thread.h"

namespace mulan::view {

RenderThread::~RenderThread() {
    stop();
}

core::Result<void> RenderThread::start(std::string /*name*/) {
    if (running_.load()) {
        return {};
    }
    if (queue_.closed()) {
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg,
                                                "RenderThread cannot be restarted after stop."));
    }

    running_.store(true);
    try {
        worker_ = std::jthread([this](std::stop_token stopToken) {
            run(stopToken);
        });
    } catch (...) {
        running_.store(false);
        queue_.close();
        return std::unexpected(core::Error::make(core::ErrorCode::Internal,
                                                "Failed to start render thread."));
    }
    return {};
}

core::Result<void> RenderThread::requestShutdown() {
    return queue_.submit(RenderTask{
        RenderTaskKind::Shutdown,
        "Shutdown",
        [] {},
    });
}

void RenderThread::stop() {
    queue_.close();
    if (worker_.joinable()) {
        worker_.request_stop();
        if (worker_.get_id() != std::this_thread::get_id()) {
            worker_.join();
        }
    }
    running_.store(false);
}

void RenderThread::run(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        auto task = queue_.waitPop(stopToken);
        if (!task) {
            break;
        }
        try {
            task->execute();
        } catch (...) {
        }
        if (task->kind() == RenderTaskKind::Shutdown) {
            break;
        }
    }
    running_.store(false);
}

} // namespace mulan::view
