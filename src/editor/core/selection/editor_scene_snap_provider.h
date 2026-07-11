#pragma once

#include "core/selection/editor_input.h"

namespace mulan::app {

class EditorSceneSnapProvider {
public:
    static void collect(const EditorSnapQuery& query, std::vector<EditorSnapCandidate>& out);
};

}  // namespace mulan::app
