/**
 * @file editor_preview_controller.h
 * @brief EditorPreviewController 统一管理编辑器对视图预览层的写入。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "../operation/draft_geometry.h"

#include <mulan/view/core/preview_layer.h>

#include <vector>

namespace mulan::view {
class ViewContext;
}

namespace mulan::editor {

class EditorPreviewController {
public:
    explicit EditorPreviewController(view::ViewContext& view) : view_(view) {}

    void clearAll();
    void clearToolGeometry();
    void setToolGeometry(DraftGeometry geometry);
    void clearToolReferences();
    void setToolReferences(std::vector<view::PreviewReference> references);
    void clearSnapGeometry();
    void setSnapGeometry(DraftGeometry geometry);
    void clearGripGeometry();
    void setGripGeometry(DraftGeometry geometry);
    void clearGripHotGeometry();
    void setGripHotGeometry(DraftGeometry geometry);

private:
    view::ViewContext& view_;
};

}  // namespace mulan::editor
