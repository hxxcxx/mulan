#pragma once

#include <mulan/asset/curve_asset.h>
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <cstddef>
#include <cstdint>

namespace mulan::app {

struct EditorGripId {
    uint64_t value = 0;

    static constexpr EditorGripId invalid() { return {}; }
    constexpr bool valid() const { return value != 0; }

    friend constexpr bool operator==(EditorGripId a, EditorGripId b) { return a.value == b.value; }
    friend constexpr bool operator!=(EditorGripId a, EditorGripId b) { return !(a == b); }
};

enum class EditorGripKind : uint8_t {
    Vertex,
    Midpoint,
    Center,
    Radius,
};

enum class EditorGripAction : uint8_t {
    MovePrimitive,
    MoveSegment,
    MoveVertex,
    ChangeRadius,
};

struct EditorGrip {
    EditorGripId id;
    scene::EntityId entity = scene::EntityId::invalid();
    asset::CurveElementId element = asset::CurveElementId::invalid();
    asset::CurvePrimitive sourcePrimitive;
    asset::CurveElementKind primitiveKind = asset::CurveElementKind::Segment;
    EditorGripKind kind = EditorGripKind::Vertex;
    EditorGripAction action = EditorGripAction::MovePrimitive;
    math::Point3 worldPosition;
    math::Point3 localPosition;
    math::Mat4 localToWorld = math::Mat4(1.0);
    math::Mat4 worldToLocal = math::Mat4(1.0);
    size_t vertexIndex = 0;
    size_t segmentIndex = 0;
    double pickRadiusPixels = 9.0;
};

}  // namespace mulan::app
