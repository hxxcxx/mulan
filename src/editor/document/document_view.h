/**
 * @file document_view.h
 * @brief 管理一个文档视图的会话绑定、视口运行时和文档渲染连接。
 *
 * @author hxxcxx
 * @date 2026-07-07 (原始) / 2026-07-15 (PImpl 与公开依赖收口)
 */
#pragma once

#include "../command/command.h"

#include <mulan/interaction/work_plane.h>
#include <mulan/view/capture/capture_request.h>
#include <mulan/view/core/view_state.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace mulan::editor {
class DocumentSession;
}

namespace mulan::engine {
enum class ProjectionMode : uint8_t;
struct InputEvent;
}  // namespace mulan::engine

namespace mulan::view {
struct ViewConfig;
}  // namespace mulan::view

namespace mulan::editor {

/// 文档输入最终由哪一层处理。该枚举是 app 层可依赖的稳定边界，避免把
/// ViewContext/EditorSession 的内部 bool 组合逻辑泄漏到 DocumentViewport。
enum class DocumentInputDisposition : uint8_t {
    Ignored = 0,
    ViewNavigation,
    ViewOverlay,
    ModalInteraction,
    EditorTool,
    Selection,
    Cancelled,
};

/// DocumentView 对一次输入的完整处理结果。
struct DocumentInputOutcome {
    DocumentInputDisposition disposition = DocumentInputDisposition::Ignored;
    bool frameInvalidated = false;
    bool commandStateInvalidated = false;

    [[nodiscard]] bool handled() const { return disposition != DocumentInputDisposition::Ignored; }
    [[nodiscard]] bool needsFrame() const { return frameInvalidated; }
    [[nodiscard]] bool needsCommandStateRefresh() const { return commandStateInvalidated; }
};

class DocumentView {
public:
    DocumentView();
    ~DocumentView();

    DocumentView(const DocumentView&) = delete;
    DocumentView& operator=(const DocumentView&) = delete;

    /// runtimeEventCallback 由 RenderThread 调用，只能执行线程安全的 owner-thread 唤醒。
    bool init(const mulan::view::ViewConfig& config, int width, int height, std::function<void()> runtimeEventCallback);
    /// 幂等解除编辑会话、文档绑定与渲染运行时；析构和应用层关闭走同一条路径。
    void shutdown();
    void resize(int width, int height);
    /// 帧调度器的最终提交入口；其他调用方应只发失效请求。
    mulan::ResultVoid renderFrame();
    /// 在文档视图 owner 线程回收渲染资源 ACK 与异步失败。
    mulan::ResultVoid consumeRenderEvents();
    void fitAll();
    /// 切换到世界 XY 正视图，并统一刷新依赖屏幕位置的编辑器覆盖层。
    void setCameraToWorldXY();
    mulan::engine::ProjectionMode projectionMode() const;
    /// 切换当前视图的投影模式；相机负责保持构图尺度，本层统一刷新编辑覆盖层与帧。
    void setProjectionMode(mulan::engine::ProjectionMode mode);

    /// 设置视图状态变化后的统一帧失效出口。
    void setFrameInvalidationCallback(std::function<void()> callback);

    bool isReady() const;

    mulan::view::RenderMode renderMode() const;
    void setRenderMode(mulan::view::RenderMode mode);
    bool viewCubeVisible() const;
    void setViewCubeVisible(bool visible);
    /// 使用当前文档视图相机执行截图，调用方不能注入陈旧相机快照。
    mulan::Result<mulan::view::CaptureImage> capture(mulan::view::CaptureRequest request);
    mulan::engine::WorkPlane viewWorkPlane() const;

    void setDocumentSession(DocumentSession* session);
    DocumentSession* session() const;

    DocumentInputOutcome handleInput(const mulan::engine::InputEvent& event);

    /// 取消当前所有临时交互（工具 / grip / camera drag / ViewCube press）。
    /// 仅在指针事务确实可能丢失后续事件时调用，例如 UngrabMouse。
    DocumentInputOutcome cancelInteraction();

    /// 视口失焦入口。只有本视口仍持有未完成鼠标事务时才整体取消；
    /// 普通焦点切换只清理 hover/snap，活动 CAD 工具保持存活。
    DocumentInputOutcome handleFocusLost();

    /// 鼠标移出视口或无鼠标键事务的失焦，只清理瞬态指针状态（click 跟踪 / hover / snap 标记）。
    /// 活动工具与相机事务保持存活：CAD 命令可跨越失焦与鼠标移出继续，重新进入后恢复正常。
    DocumentInputOutcome clearTransientInteraction();

    // ── 编辑器交互（转发，app 层不直接接触 EditorSession）──

    /// 构造命令宿主，供 CommandManager 执行命令。
    mulan::editor::CommandHost commandHost();

private:
    friend class CommandManager;

    struct Impl;

    /// 在 handleInput 内部跟踪左键 press，release 时据此判定 click-vs-drag 选择。
    void trackPressEvent(const mulan::engine::InputEvent& event);
    bool maybeSelectOnRelease(const mulan::engine::InputEvent& event, bool allowSelection);
    void clearClickTracking();
    bool isLeftDragExceedingThreshold(const mulan::engine::InputEvent& event) const;
    void trackPointerButtons(const mulan::engine::InputEvent& event);
    void rebuildAttachment();
    void invalidateFrame() const;
    void onCommandCompleted();
    void updateHoverAtFramebuffer(double x, double y);
    void selectAtFramebuffer(double x, double y);

    std::unique_ptr<Impl> impl_;
};

}  // namespace mulan::editor
