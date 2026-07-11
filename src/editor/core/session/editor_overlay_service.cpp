#include "core/session/editor_overlay_service.h"

#include <utility>

namespace mulan::app {

void EditorOverlayService::bind(view::ViewContext* view) {
    preview_.bind(view);
}

void EditorOverlayService::unbind() {
    preview_.unbind();
}

void EditorOverlayService::clearAll() {
    preview_.clearAll();
}

void EditorOverlayService::clear(EditorOverlayRole role) {
    switch (role) {
    case EditorOverlayRole::Tool:
        preview_.clearToolGeometry();
        preview_.clearToolReferences();
        return;
    case EditorOverlayRole::Snap: preview_.clearSnapGeometry(); return;
    case EditorOverlayRole::Grip: preview_.clearGripGeometry(); return;
    case EditorOverlayRole::GripHot: preview_.clearGripHotGeometry(); return;
    }
}

void EditorOverlayService::submit(EditorOverlaySubmission submission) {
    switch (submission.role) {
    case EditorOverlayRole::Tool: preview_.setToolGeometry(std::move(submission.geometry)); return;
    case EditorOverlayRole::Snap: preview_.setSnapGeometry(std::move(submission.geometry)); return;
    case EditorOverlayRole::Grip: preview_.setGripGeometry(std::move(submission.geometry)); return;
    case EditorOverlayRole::GripHot: preview_.setGripHotGeometry(std::move(submission.geometry)); return;
    }
}

void EditorOverlayService::submit(EditorOverlayReferenceSubmission submission) {
    switch (submission.role) {
    case EditorOverlayRole::Tool: preview_.setToolReferences(std::move(submission.references)); return;
    case EditorOverlayRole::Snap:
    case EditorOverlayRole::Grip:
    case EditorOverlayRole::GripHot: return;
    }
}

}  // namespace mulan::app
