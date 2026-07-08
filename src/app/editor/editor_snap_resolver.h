#pragma once

#include "editor_input.h"

#include <span>

namespace mulan::app {

struct EditorSnapResolveInput {
    std::span<const EditorSnapCandidate> candidates;
    EditorPointPolicy pointPolicy;
    EditorSnapSettings snapSettings;
};

class EditorSnapResolver {
public:
    static std::optional<EditorPoint> resolve(const EditorSnapResolveInput& input);
};

}  // namespace mulan::app
