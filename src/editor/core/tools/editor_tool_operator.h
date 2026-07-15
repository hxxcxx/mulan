/**
 * @file editor_tool_operator.h
 * @brief 把 EditorTool 包成 engine::Operator 的适配器。
 *
 * 这是 editor 层 → engine/view 层的桥梁：当活动编辑工具存在时，一个
 * EditorToolOperator 被 push 到 ViewContext::op_stack_ 栈顶，独占 pointer 事件。
 * 编辑副作用（operation / preview）在 editor 层闭环（经 EditorSession::driveActiveTool
 * → applyAction），engine 层只见 bool（handled），不认识 DocumentOperation。
 *
 * 相机穿透：工具激活期间，中键/右键/滚轮经 isCameraEvent/handleCameraEvent
 * 钩子转发给内部持有的 CameraManipulator，使导航手势不打断绘制。
 *
 * @author hxxcxx
 * @date 2026-07-14
 */
#pragma once

#include <mulan/interaction/operator.h>
#include <mulan/interaction/camera_manipulator.h>

namespace mulan::editor {

class EditorSession;

/// 适配器：把当前 EditorSession 的活动工具暴露为栈上 Operator。
///
/// 生命周期由 EditorSession/DocumentView 管理：
///   - startTool 时 push（含工具 begin action）
///   - 工具 finish/cancel 时由 ViewContext 检测 isFinished 后 pop
class EditorToolOperator : public engine::Operator {
public:
    explicit EditorToolOperator(EditorSession& session);

    bool handleEvent(const engine::InputEvent& e, engine::Camera& cam) override;

    /// 工具激活期间，中键/右键/滚轮判定为相机事件，穿透给内部 CameraManipulator。
    /// 注意 button/buttons 语义：press/release 查 button（本次变化），
    /// move 查 buttons（按住集合，move 的 button 恒为 None）。
    bool isCameraEvent(const engine::InputEvent& e) const override;

    /// 相机事件转发给内部 CameraManipulator。
    bool handleCameraEvent(const engine::InputEvent& e, engine::Camera& cam) override;

    /// 取消：经 EditorSession 取消活动工具并清理。幂等。
    bool onCancel() override;

private:
    EditorSession& session_;
    engine::CameraManipulator camera_delegate_;  ///< 相机穿透转发目标
};

}  // namespace mulan::editor
