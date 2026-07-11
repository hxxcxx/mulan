#pragma once

#include "../selection/editor_input.h"

#include <span>

namespace mulan::editor {

struct EditorSnapResolveInput {
    std::span<const EditorSnapCandidate> candidates;
    EditorPointPolicy pointPolicy;
    EditorSnapSettings snapSettings;
};

class EditorSnapResolver {
public:
    static EditorSnapResult resolveResult(const EditorSnapResolveInput& input);
    static std::optional<EditorPoint> resolve(const EditorSnapResolveInput& input);
};

}  // namespace mulan::editor
