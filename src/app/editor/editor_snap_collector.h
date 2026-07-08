#pragma once

#include "editor_input.h"

#include <optional>

namespace mulan::app {

struct EditorSnapCollectInput {
    EditorSnapQuery query;
};

class EditorSnapCollector {
public:
    static void collect(const EditorSnapCollectInput& input, std::vector<EditorSnapCandidate>& out);
};

}  // namespace mulan::app
