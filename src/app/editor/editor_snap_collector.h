#pragma once

#include "editor_input.h"

#include <optional>

namespace mulan::app {

struct EditorSnapCollectInput {
    const engine::InputEvent& event;
    const engine::WorkPlane& workPlane;
    std::optional<math::Point3> workPoint;
    std::optional<EditorPickHit> pickHit;
    EditorPointPolicy pointPolicy;
    EditorSnapSettings snapSettings;
};

class EditorSnapCollector {
public:
    static void collect(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out);
};

}  // namespace mulan::app
