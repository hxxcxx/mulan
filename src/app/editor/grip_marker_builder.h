#pragma once

#include "draft_geometry.h"
#include "editor_grip.h"

#include <mulan/engine/render/camera/camera.h>

#include <optional>
#include <span>

namespace mulan::app {

class GripMarkerBuilder {
public:
    static DraftGeometry build(std::span<const EditorGrip> grips, const engine::Camera& camera,
                               std::optional<EditorGripId> excludedGrip = std::nullopt);
    static DraftGeometry buildHot(const EditorGrip& grip, const engine::Camera& camera);
};

}  // namespace mulan::app
