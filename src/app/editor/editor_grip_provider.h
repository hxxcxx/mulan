#pragma once

#include "editor_grip.h"

#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::app {

class EditorGripProvider {
public:
    std::vector<EditorGrip> build(const io::Document& document) const;
};

}  // namespace mulan::app
