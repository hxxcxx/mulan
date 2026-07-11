/**
 * @file editor_grip_controller.h
 * @brief EditorGripController 管理可编辑夹点状态、拾取检测和预览标记。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "core/grip/editor_grip.h"
#include "core/grip/editor_grip_provider.h"
#include "core/selection/editor_selection.h"

#include <optional>
#include <vector>

class DocumentSession;

namespace mulan::view {
class ViewContext;
}

namespace mulan::app {

class EditorOverlayService;

class EditorGripController {
public:
    void bind(DocumentSession* session, view::ViewContext* view, EditorOverlayService* overlays);
    void unbind();

    void refresh(const EditorSelectionContext& selection, bool enabled);
    void clear();

    bool updateHoverAtFramebuffer(double screenX, double screenY);
    void clearHover();
    std::optional<EditorGrip> pickAtFramebuffer(double screenX, double screenY) const;

private:
    void rebuildPreview();
    const EditorGrip* gripById(EditorGripId id) const;

    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    EditorOverlayService* overlays_ = nullptr;
    EditorGripProvider provider_;
    std::vector<EditorGrip> grips_;
    std::optional<EditorGripId> hovered_;
};

}  // namespace mulan::app
