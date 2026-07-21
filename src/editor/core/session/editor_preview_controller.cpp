#include "editor_preview_controller.h"

#include <mulan/view/core/preview_layer.h>
#include <mulan/view/core/view_context.h>

namespace mulan::editor {

void EditorPreviewController::clearAll() {
    view_.clearPreview();
}

void EditorPreviewController::clearToolGeometry() {
    view_.previewLayer().clearToolGeometry();
}

void EditorPreviewController::setToolGeometry(DraftGeometry geometry) {
    if (geometry.empty()) {
        view_.previewLayer().clearToolGeometry();
        return;
    }
    view_.previewLayer().setGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

void EditorPreviewController::clearToolReferences() {
    view_.previewLayer().clearReferences();
}

void EditorPreviewController::setToolReferences(std::vector<view::PreviewReference> references) {
    if (references.empty()) {
        view_.previewLayer().clearReferences();
        return;
    }
    view_.previewLayer().setReferences(std::move(references));
}

void EditorPreviewController::clearSnapGeometry() {
    view_.previewLayer().clearSnapGeometry();
}

void EditorPreviewController::setSnapGeometry(DraftGeometry geometry) {
    if (geometry.empty()) {
        view_.previewLayer().clearSnapGeometry();
        return;
    }
    view_.previewLayer().setSnapGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

void EditorPreviewController::clearGripGeometry() {
    view_.previewLayer().clearGripGeometry();
}

void EditorPreviewController::setGripGeometry(DraftGeometry geometry) {
    if (geometry.empty()) {
        view_.previewLayer().clearGripGeometry();
        return;
    }
    view_.previewLayer().setGripGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

void EditorPreviewController::clearGripHotGeometry() {
    view_.previewLayer().clearGripHotGeometry();
}

void EditorPreviewController::setGripHotGeometry(DraftGeometry geometry) {
    if (geometry.empty()) {
        view_.previewLayer().clearGripHotGeometry();
        return;
    }
    view_.previewLayer().setGripHotGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

}  // namespace mulan::editor
