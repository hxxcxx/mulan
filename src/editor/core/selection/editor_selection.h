/**
 * @file editor_selection.h
 * @brief 编辑器选择系统相关定义
 * @author hxxcxx
 */

#pragma once

#include "editor_input.h"
#include "../operation/selection_target.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::editor {

struct EditorSelectionFilter {
    bool allowEntities = true;
    bool allowCurves = true;
    bool allowMeshes = true;
    bool allowSurfaces = true;
    bool allowSolids = true;
    bool allowVertices = true;
    bool allowEdges = true;
    bool allowFaces = true;
};

struct EditorSelectionHit {
    EditorSelectionReference reference;
    EditorPickHit pick;

    bool valid() const { return reference.valid(); }
};

class EditorSelectionContext {
public:
    const EditorSelectionFilter& filter() const { return filter_; }
    void setFilter(EditorSelectionFilter filter);

    const std::optional<EditorSelectionHit>& hovered() const { return hovered_; }
    bool setHovered(std::optional<EditorSelectionHit> hit);
    bool clearHover();

    std::span<const EditorSelectionReference> selected() const { return selected_; }
    std::optional<EditorSelectionReference> primary() const;
    bool empty() const { return selected_.empty(); }
    bool contains(scene::EntityId entity) const;

    bool selectSingle(EditorSelectionHit hit);
    bool selectSingle(EditorSelectionReference reference);
    bool clearSelection();
    bool pruneInvalid(const io::Document& document);
    void clear();

private:
    bool accepts(const EditorSelectionReference& reference) const;

    EditorSelectionFilter filter_;
    std::optional<EditorSelectionHit> hovered_;
    std::vector<EditorSelectionReference> selected_;
};

bool sameSelectionReference(const EditorSelectionReference& lhs, const EditorSelectionReference& rhs);
EditorSelectionHit makeEditorSelectionHit(const EditorPickHit& pick, const io::Document& document);

}  // namespace mulan::editor
