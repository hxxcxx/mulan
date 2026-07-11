#pragma once

#include "../operation/draft_geometry.h"
#include "../selection/editor_input.h"

namespace mulan::editor {

class SnapMarkerBuilder {
public:
    static DraftGeometry build(const EditorInput& input);
};

}  // namespace mulan::editor
