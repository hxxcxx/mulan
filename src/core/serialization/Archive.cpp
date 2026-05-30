/**
 * @file Archive.cpp
 * @brief InputArchive 非内联方法实现（路径栈、错误上下文）
 */

#include "Archive.h"

namespace mulan::core {

// ============================================================
// InputArchive 路径栈实现
// ============================================================

void InputArchive::onPathKey(std::string_view name) {
    m_pathStack.push_back({PathEntry::Key, std::string(name), 0});
}

void InputArchive::onPathBeginArray() {
    m_pathStack.push_back({PathEntry::Array, {}, 0});
}

void InputArchive::onPathAdvanceIndex() {
    if (!m_pathStack.empty() && m_pathStack.back().type == PathEntry::Array) {
        ++m_pathStack.back().index;
    }
}

void InputArchive::onPathPop() {
    if (!m_pathStack.empty()) {
        m_pathStack.pop_back();
    }
}

std::string InputArchive::buildPath() const {
    std::string path;
    for (const auto& entry : m_pathStack) {
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
