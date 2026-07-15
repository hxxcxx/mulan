/**
 * @file document_view.h
 * @brief 管理一个文档视图的会话绑定、视口运行时和文档渲染连接。
 *
 * @author hxxcxx
 * @date 2026-07-07 (原始) / 2026-07-15 (PImpl 与公开依赖收口)
 */
#pragma once

#include "../command/command.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

class DocumentSession;

namespace mulan::engine {
struct InputEvent;
}

namespace mulan::view {
struct ViewConfig;
class ViewContext;
}  // namespace mulan::view

/// 文档输入最终由哪一层处理。该枚举是 app 层可依赖的稳定边界，避免把
/// ViewContext/EditorSession 的内部 bool 组合逻辑泄漏到 DocWidget。
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

    bool init(const mulan::view::ViewConfig& config, int width, int height);
    void resize(int width, int height);
    /// 帧调度器的最终提交入口；其他调用方应只发失效请求。
    void renderFrame();
    /// 在文档视图 owner 线程回收渲染资源 ACK 与异步失败。
    mulan::Result<void> pollRenderRuntime();
    void fitAll();
    /// 切换到世界 XY 正视图，并统一刷新依赖屏幕位置的编辑器覆盖层。
    void setCameraToWorldXY();

    /// 设置视图状态变化后的统一帧失效出口。
    void setFrameInvalidationCallback(std::function<void()> callback);

    bool isInitialized() const;

    void setDocumentSession(DocumentSession* session);
    DocumentSession* session() const;

    mulan::view::ViewContext& viewContext();
    const mulan::view::ViewContext& viewContext() const;

    DocumentInputOutcome handleInput(const mulan::engine::InputEvent& event);

    /// 取消当前所有临时交互（工具 / grip / camera drag / ViewCube press）。
    /// 供 DocWidget 在 FocusOut / UngrabMouse / WindowDeactivate / leaveEvent 调用。
    DocumentInputOutcome cancelInteraction();

    // ── 编辑器交互（转发，app 层不直接接触 EditorSession）──

    bool isEditorReady() const;
    bool hasActiveEditorTool() const;
    std::string_view activeEditorToolId() const;
    void cancelActiveEditorTool();
    void clearEditorHover();
    bool canEditorUndo() const;
    bool canEditorRedo() const;

    /// 构造命令宿主，供 CommandManager 执行命令。
    mulan::editor::CommandHost commandHost();

    void updateHoverAtFramebuffer(double x, double y);
    void selectAtFramebuffer(double x, double y);

private:
    struct Impl;

    /// 在 handleInput 内部跟踪左键 press，release 时据此判定 click-vs-drag 选择。
    void trackPressEvent(const mulan::engine::InputEvent& event);
    bool maybeSelectOnRelease(const mulan::engine::InputEvent& event, bool allowSelection);
    void clearClickTracking();
    bool isLeftDragExceedingThreshold(const mulan::engine::InputEvent& event) const;
    void invalidateFrame() const;

    std::unique_ptr<Impl> impl_;
};
