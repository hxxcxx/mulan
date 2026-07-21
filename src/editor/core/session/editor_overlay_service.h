/**
 * @file editor_overlay_service.h
 * @brief EditorOverlayService 统一提交编辑器临时可视对象。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "../operation/draft_geometry.h"
#include "editor_preview_controller.h"

#include <mulan/view/core/preview_layer.h>

#include <utility>
#include <vector>

namespace mulan::view {
class ViewContext;
}

namespace mulan::editor {

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

struct EditorOverlayReferenceSubmission {
    EditorOverlayReferenceSubmission(EditorOverlayRole role_, std::vector<view::PreviewReference> references_)
        : role(role_), references(std::move(references_)) {}

    EditorOverlayRole role = EditorOverlayRole::Tool;
    std::vector<view::PreviewReference> references;
};

class EditorOverlayService {
public:
    explicit EditorOverlayService(view::ViewContext& view) : preview_(view) {}

    void clearAll();
    void clear(EditorOverlayRole role);
    void submit(EditorOverlaySubmission submission);
    void submit(EditorOverlayReferenceSubmission submission);

private:
    EditorPreviewController preview_;
};

}  // namespace mulan::editor
