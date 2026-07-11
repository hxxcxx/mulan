#pragma once

#include "core/grip/editor_grip.h"
#include "core/selection/editor_selection.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::app {

struct EditorGripBuildContext {
    const io::Document& document;
    const EditorSelectionContext& selection;
    uint64_t& nextId;
};

class EditorGripSource {
public:
    virtual ~EditorGripSource() = default;
    virtual void build(const EditorGripBuildContext& context, std::vector<EditorGrip>& out) const = 0;
};

class EditorGripProvider {
public:
    EditorGripProvider();
    ~EditorGripProvider();

    EditorGripProvider(const EditorGripProvider&) = delete;
    EditorGripProvider& operator=(const EditorGripProvider&) = delete;

    void addSource(std::unique_ptr<EditorGripSource> source);
    std::vector<EditorGrip> build(const io::Document& document, const EditorSelectionContext& selection) const;

private:
    std::vector<std::unique_ptr<EditorGripSource>> sources_;
};

}  // namespace mulan::app
