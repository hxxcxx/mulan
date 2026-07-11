#pragma once

#include "../selection/editor_input.h"

#include <optional>

namespace mulan::editor {

struct EditorSnapCollectInput {
    EditorSnapQuery query;
};

class EditorSnapCollector {
public:
    static void collect(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out);
};

}  // namespace mulan::editor
