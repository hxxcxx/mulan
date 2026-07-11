#pragma once

#include "editor_input.h"

namespace mulan::editor {

class EditorSceneSnapProvider {
public:
    static void collect(const EditorSnapQuery& query, std::vector<EditorSnapCandidate>& out);
};

}  // namespace mulan::editor
