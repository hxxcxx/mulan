#pragma once

#include "core/operation/draft_geometry.h"
#include "core/selection/editor_input.h"

namespace mulan::app {

class SnapMarkerBuilder {
public:
    static DraftGeometry build(const EditorInput& input);
};

}  // namespace mulan::app
