#include "editor_grip_controller.h"

#include "editor_overlay_service.h"
#include "grip_marker_builder.h"
#include "document/document_session.h"

#include <mulan/io/document.h>
#include <mulan/view/core/view_context.h>

#include <cmath>
#include <utility>

namespace mulan::app {
namespace {

struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
};

std::optional<ScreenPoint> projectToScreen(const engine::Camera& camera, const math::Point3& world) {
    const math::Vec4 clip = camera.viewProjectionMatrix() * math::Vec4(world.x, world.y, world.z, 1.0);
    if (std::abs(clip.w) <= 1.0e-12) {
        return std::nullopt;
    }

    const double ndcX = clip.x / clip.w;
    const double ndcY = clip.y / clip.w;
    const double ndcZ = clip.z / clip.w;
    if (ndcZ < -1.0 || ndcZ > 1.0) {
        return std::nullopt;
    }

    return ScreenPoint{
        .x = (ndcX + 1.0) * 0.5 * static_cast<double>(camera.width()),
        .y = (1.0 - ndcY) * 0.5 * static_cast<double>(camera.height()),
        .depth = ndcZ,
    };
}

}  // namespace

void EditorGripController::bind(DocumentSession* session, view::ViewContext* view, EditorOverlayService* overlays) {
    session_ = session;
    view_ = view;
    overlays_ = overlays;
}

void EditorGripController::unbind() {
    clear();
    session_ = nullptr;
    view_ = nullptr;
    overlays_ = nullptr;
}

void EditorGripController::refresh(const EditorSelectionContext& selection, bool enabled) {
    if (!enabled || !session_ || !session_->document() || !view_ || !view_->isInitialized()) {
        clear();
        return;
    }

    grips_ = provider_.build(*session_->document(), selection);
    if (hovered_ && !gripById(*hovered_)) {
        hovered_.reset();
    }
    rebuildPreview();
}

void EditorGripController::clear() {
    grips_.clear();
    hovered_.reset();
    if (overlays_) {
        overlays_->clear(EditorOverlayRole::Grip);
        overlays_->clear(EditorOverlayRole::GripHot);
    }
}

bool EditorGripController::updateHoverAtFramebuffer(double screenX, double screenY) {
    if (!view_ || !view_->isInitialized()) {
        clearHover();
        return false;
    }

    const std::optional<EditorGrip> grip = pickAtFramebuffer(screenX, screenY);
    const std::optional<EditorGripId> previous = hovered_;
    hovered_ = grip ? std::optional<EditorGripId>(grip->id) : std::nullopt;
    if (hovered_ != previous) {
        rebuildPreview();
    }
    return grip.has_value();
}

void EditorGripController::clearHover() {
    if (!hovered_) {
        return;
    }
    hovered_.reset();
    rebuildPreview();
}

std::optional<EditorGrip> EditorGripController::pickAtFramebuffer(double screenX, double screenY) const {
    if (!view_) {
        return std::nullopt;
    }

    const engine::Camera& camera = view_->camera();
    std::optional<EditorGrip> best;
    double bestDistanceSq = 0.0;
    double bestDepth = 0.0;
    for (const EditorGrip& grip : grips_) {
        const auto screen = projectToScreen(camera, grip.worldPosition);
        if (!screen) {
            continue;
        }

        const double dx = screen->x - screenX;
        const double dy = screen->y - screenY;
        const double distanceSq = dx * dx + dy * dy;
        const double radiusSq = grip.pickRadiusPixels * grip.pickRadiusPixels;
        if (distanceSq > radiusSq) {
            continue;
        }

        if (!best || distanceSq < bestDistanceSq ||
            (std::abs(distanceSq - bestDistanceSq) <= 1.0e-6 && screen->depth < bestDepth)) {
            best = grip;
            bestDistanceSq = distanceSq;
            bestDepth = screen->depth;
        }
    }
    return best;
}

void EditorGripController::rebuildPreview() {
    if (!overlays_ || !view_) {
        return;
    }

    const EditorGrip* hotGrip = hovered_ ? gripById(*hovered_) : nullptr;

    DraftGeometry gripGeometry = GripMarkerBuilder::build(
            grips_, view_->camera(), hotGrip ? std::optional<EditorGripId>(hotGrip->id) : std::nullopt);
    overlays_->submit(EditorOverlaySubmission(EditorOverlayRole::Grip, std::move(gripGeometry)));

    if (!hotGrip) {
        overlays_->clear(EditorOverlayRole::GripHot);
        return;
    }

    DraftGeometry hotGeometry = GripMarkerBuilder::buildHot(*hotGrip, view_->camera());
    overlays_->submit(EditorOverlaySubmission(EditorOverlayRole::GripHot, std::move(hotGeometry)));
}

const EditorGrip* EditorGripController::gripById(EditorGripId id) const {
    for (const EditorGrip& grip : grips_) {
        if (grip.id == id) {
            return &grip;
        }
    }
    return nullptr;
}

}  // namespace mulan::app
