#include "archive.h"

namespace mulan::core {

// ============================================================
// InputArchive 路径栈实现
// ============================================================

void InputArchive::onPathKey(std::string_view name) {
    path_stack_.push_back({PathEntry::Key, std::string(name), 0});
}

void InputArchive::onPathBeginArray() {
    path_stack_.push_back({PathEntry::Array, {}, 0});
}

void InputArchive::onPathAdvanceIndex() {
    if (!path_stack_.empty() && path_stack_.back().type == PathEntry::Array) {
        ++path_stack_.back().index;
    }
}

void InputArchive::onPathPop() {
    if (!path_stack_.empty()) {
        path_stack_.pop_back();
    }
}

std::string InputArchive::buildPath() const {
    std::string path;
    for (const auto& entry : path_stack_) {
        if (entry.type == PathEntry::Key) {
            if (!path.empty()) path += '.';
            path += entry.name;
        } else {
            path += '[';
            path += std::to_string(entry.index);
            path += ']';
        }
    }
    return path;
}

} // namespace mulan::core
