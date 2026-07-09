#include "editor_preview_controller.h"

#include <mulan/view/preview_layer.h>
#include <mulan/view/view_context.h>

namespace mulan::app {

void EditorPreviewController::clearAll() {
    if (view_) {
        view_->clearPreview();
    }
}

void EditorPreviewController::clearToolGeometry() {
    if (view_) {
        view_->previewLayer().clearToolGeometry();
    }
}

void EditorPreviewController::setToolGeometry(DraftGeometry geometry) {
    if (!view_) {
        return;
    }
    if (geometry.empty()) {
        view_->previewLayer().clearToolGeometry();
        return;
    }
    view_->previewLayer().setGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

void EditorPreviewController::clearSnapGeometry() {
    if (view_) {
        view_->previewLayer().clearSnapGeometry();
    }
}

void EditorPreviewController::setSnapGeometry(DraftGeometry geometry) {
    if (!view_) {
        return;
    }
    if (geometry.empty()) {
        view_->previewLayer().clearSnapGeometry();
        return;
    }
    view_->previewLayer().setSnapGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

void EditorPreviewController::clearGripGeometry() {
    if (view_) {
        view_->previewLayer().clearGripGeometry();
    }
}

void EditorPreviewController::setGripGeometry(DraftGeometry geometry) {
    if (!view_) {
        return;
    }
    if (geometry.empty()) {
        view_->previewLayer().clearGripGeometry();
        return;
    }
    view_->previewLayer().setGripGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

void EditorPreviewController::clearGripHotGeometry() {
    if (view_) {
        view_->previewLayer().clearGripHotGeometry();
    }
}

void EditorPreviewController::setGripHotGeometry(DraftGeometry geometry) {
    if (!view_) {
        return;
    }
    if (geometry.empty()) {
        view_->previewLayer().clearGripHotGeometry();
        return;
    }
    view_->previewLayer().setGripHotGeometry(geometry.takeCurves(), geometry.takeMeshes());
}

}  // namespace mulan::app
