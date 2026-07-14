/**
 * @file drag_from_anchor_tool.h
 * @brief 锚点拖拽编辑工具的共享骨架。
 *
 * TransformTool 与 GripDragTool 共享同一套交互骨架：右键取消、release 优先于
 * worldPoint（修 P7）、无 worldPoint 时吞事件、move 时更新预览。两者只在
 * 「如何设定锚点」「如何计算编辑增量」「如何提交」上不同。
 *
 * 本基类独占 handleInput 的分发框架，子类通过 4 个纯虚钩子填入编辑语义：
 *   - onAnchorPress：左键 press 语义（首次设锚点 / 吞掉）
 *   - updateDragPreview：move 时计算增量并返回预览 action
 *   - commitAtPoint：release 有 worldPoint 时的提交
 *   - commitFallback：release 无 worldPoint 时的回退提交（用上次记录的增量/图元）
 *
 * 与 PointDrawingTool 对称：后者统一「点采集」模式，本类统一「锚点拖拽」模式。
 * 未来 translate/rotate/scale Gizmo 也将继承本类。
 *
 * @author hxxcxx
 * @date 2026-07-15
 */
#pragma once

#include "editor_tool.h"

namespace mulan::editor {

class DragFromAnchorTool : public EditorTool {
public:
    EditorAction handleInput(const EditorInput& input) final;

protected:
    /// 左键 press 的语义。TransformTool 首次 press 设定锚点、再次 press 提交；GripDragTool 恒吞事件。
    virtual EditorAction onAnchorPress(const EditorInput& input, const math::Point3& worldPoint) = 0;

    /// move 时计算编辑增量并返回预览 action。
    virtual EditorAction updateDragPreview(const EditorInput& input, const math::Point3& worldPoint) = 0;

    /// release 有 worldPoint 时的提交。
    virtual EditorAction commitAtPoint(const EditorInput& input, const math::Point3& worldPoint) = 0;

    /// release 无 worldPoint 时的回退提交（用上次记录的增量/图元），确保 release 总能完成。
    /// 返回 nullopt 表示无可提交内容，基类将转为 cancel。
    virtual std::optional<EditorAction> commitFallback(const EditorInput& input) = 0;
};

}  // namespace mulan::editor
