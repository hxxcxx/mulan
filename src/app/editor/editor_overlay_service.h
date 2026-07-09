/**
 * @file editor_overlay_service.h
 * @brief EditorOverlayService 统一提交编辑器临时可视对象。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "draft_geometry.h"
#include "editor_preview_controller.h"

#include <utility>

namespace mulan::view {
class ViewContext;
}

namespace mulan::app {

enum class EditorOverlayRole {
    Tool,
    Snap,
    Grip,
    GripHot,
};

struct EditorOverlaySubmission {
    EditorOverlaySubmission(EditorOverlayRole role_, DraftGeometry geometry_)
        : role(role_), geometry(std::move(geometry_)) {}

    EditorOverlayRole role = EditorOverlayRole::Tool;
    DraftGeometry geometry;
};

class EditorOverlayService {
public:
    void bind(view::ViewContext* view);
    void unbind();

    bool isBound() const { return preview_.isBound(); }

    void clearAll();
    void clear(EditorOverlayRole role);
    void submit(EditorOverlaySubmission submission);

private:
    EditorPreviewController preview_;
};

}  // namespace mulan::app
