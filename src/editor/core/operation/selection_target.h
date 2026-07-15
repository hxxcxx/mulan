/**
 * @file selection_target.h
 * @brief SelectionTarget 描述编辑器选择对象及其子对象身份。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include <mulan/asset/curve_asset.h>
#include <mulan/render/frontend/pick_identity.h>
#include <mulan/scene/entity_id.h>

#include <cstddef>
#include <cstdint>

namespace mulan::editor {

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
    SurfaceFace,
    SurfaceEdge,
    SurfaceVertex,
    SolidFace,
    SolidEdge,
    SolidVertex,
    ControlPoint,
    Grip,
};

struct SubObjectKey {
    asset::CurveElementId curveElement = asset::CurveElementId::invalid();
    asset::CurveElementKind curveKind = asset::CurveElementKind::Segment;
    size_t sourceDrawableIndex = 0;
    bool hasSourceDrawableIndex = false;
    size_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    size_t componentIndex = 0;
    bool hasComponentIndex = false;
    double parameter = 0.0;

    bool curveElementSelection() const { return curveElement.valid(); }

    friend bool operator==(const SubObjectKey& lhs, const SubObjectKey& rhs) {
        return lhs.curveElement == rhs.curveElement && lhs.curveKind == rhs.curveKind &&
               lhs.sourceDrawableIndex == rhs.sourceDrawableIndex &&
               lhs.hasSourceDrawableIndex == rhs.hasSourceDrawableIndex && lhs.primitiveIndex == rhs.primitiveIndex &&
               lhs.hasPrimitiveIndex == rhs.hasPrimitiveIndex && lhs.componentIndex == rhs.componentIndex &&
               lhs.hasComponentIndex == rhs.hasComponentIndex;
    }

    friend bool operator!=(const SubObjectKey& lhs, const SubObjectKey& rhs) { return !(lhs == rhs); }
};

struct SelectionTarget {
    scene::EntityId entity = scene::EntityId::invalid();
    engine::PickId pickId;
    EditorSelectionDomain domain = EditorSelectionDomain::Entity;
    EditorSubEntityKind kind = EditorSubEntityKind::Entity;
    SubObjectKey subObject;

    bool valid() const { return static_cast<bool>(entity); }
    bool wholeEntity() const { return valid() && kind == EditorSubEntityKind::Entity; }
    bool curveElementSelection() const {
        return domain == EditorSelectionDomain::Curve && subObject.curveElementSelection();
    }
    /// PickId 由 RenderScene 单调分配，不能从可复用的 EntityId::index() 重新推导。
    engine::PickId renderPickId() const { return pickId; }

    friend bool operator==(const SelectionTarget& lhs, const SelectionTarget& rhs) {
        return lhs.entity == rhs.entity && lhs.pickId == rhs.pickId && lhs.domain == rhs.domain &&
               lhs.kind == rhs.kind && lhs.subObject == rhs.subObject;
    }

    friend bool operator!=(const SelectionTarget& lhs, const SelectionTarget& rhs) { return !(lhs == rhs); }
};

using EditorSelectionReference = SelectionTarget;

}  // namespace mulan::editor
