#pragma once

#include "draft_geometry.h"
#include "editor_input.h"

namespace mulan::app {

class SnapMarkerBuilder {
public:
    static DraftGeometry build(const EditorInput& input);
};

}  // namespace mulan::app
