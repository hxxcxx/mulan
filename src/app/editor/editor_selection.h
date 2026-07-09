/**
 * @file editor_selection.h
 * @brief 编辑器选择系统相关定义
 * @author hxxcxx
 */

#pragma once

#include "editor_input.h"

#include <mulan/asset/curve_asset.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::app {

enum class EditorSelectionDomain : uint8_t {
    Entity,
    Curve,
    Mesh,
    Surface,
    Solid,
};

enum class EditorSubEntityKind : uint8_t {
    Entity,
    CurveElement,
    CurveSegment,
    CurveVertex,
    MeshFace,
    MeshEdge,
    MeshVertex,
};

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

struct EditorSelectionReference {
    scene::EntityId entity = scene::EntityId::invalid();
    EditorSelectionDomain domain = EditorSelectionDomain::Entity;
    EditorSubEntityKind kind = EditorSubEntityKind::Entity;
    engine::PickId pickId;
    asset::CurveElementId curveElement = asset::CurveElementId::invalid();
    asset::CurveElementKind curveKind = asset::CurveElementKind::Segment;
    size_t sourceDrawableIndex = 0;
    size_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    size_t componentIndex = 0;
    bool hasComponentIndex = false;
    double parameter = 0.0;

    bool valid() const { return static_cast<bool>(entity); }
    bool wholeEntity() const { return valid() && kind == EditorSubEntityKind::Entity; }
    bool curveElementSelection() const { return domain == EditorSelectionDomain::Curve && curveElement.valid(); }
    engine::PickId renderPickId() const {
        if (pickId.valid()) {
            return pickId;
        }
        return entity ? engine::PickId::fromValue(entity.index()) : engine::PickId::invalid();
    }
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
    void clear();

private:
    bool accepts(const EditorSelectionReference& reference) const;

    EditorSelectionFilter filter_;
    std::optional<EditorSelectionHit> hovered_;
    std::vector<EditorSelectionReference> selected_;
};

bool sameSelectionReference(const EditorSelectionReference& lhs, const EditorSelectionReference& rhs);
EditorSelectionHit makeEditorSelectionHit(const EditorPickHit& pick, const io::Document& document);

}  // namespace mulan::app
