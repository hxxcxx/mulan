/**
 * @file render_task.h
 * @brief RenderTask 描述提交到渲染线程执行的一项命令。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <functional>
#include <string>
#include <utility>

namespace mulan::view {

enum class RenderTaskKind {
    SubmitFrame,
    ResizeSurface,
    CaptureFrame,
    UpdateSceneSnapshot,
    Shutdown,
    Custom,
};

class RenderTask {
public:
    using Work = std::function<void()>;

    RenderTask() = default;
    RenderTask(RenderTaskKind kind, std::string label, Work work)
        : kind_(kind), label_(std::move(label)), work_(std::move(work)) {}

    RenderTaskKind kind() const { return kind_; }
    const std::string& label() const { return label_; }
    explicit operator bool() const { return static_cast<bool>(work_); }

    void execute() {
        if (work_) {
            work_();
        }
    }

private:
    RenderTaskKind kind_ = RenderTaskKind::Custom;
    std::string label_;
    Work work_;
};

}  // namespace mulan::view
