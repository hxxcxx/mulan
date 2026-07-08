#include "editor_grip_provider.h"

#include <mulan/asset/asset_library.h>
#include <mulan/io/document.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/scene.h>

#include <utility>
#include <variant>

namespace mulan::app {
namespace {

struct GripBuildContext {
    std::vector<EditorGrip>& grips;
    scene::EntityId entity = scene::EntityId::invalid();
    math::Mat4 localToWorld = math::Mat4(1.0);
    math::Mat4 worldToLocal = math::Mat4(1.0);
    uint64_t nextId = 1;
};

EditorGrip makeGrip(GripBuildContext& context, const asset::CurveElement& element, EditorGripKind kind,
                    EditorGripAction action, const math::Point3& localPosition) {
    EditorGrip grip;
    grip.id = EditorGripId{ context.nextId++ };
    grip.entity = context.entity;
    grip.element = element.id;
    grip.sourcePrimitive = element.primitive;
    grip.primitiveKind = element.primitive.kind();
    grip.kind = kind;
    grip.action = action;
    grip.localPosition = localPosition;
    grip.worldPosition = localPosition.transformedBy(context.localToWorld);
    grip.localToWorld = context.localToWorld;
    grip.worldToLocal = context.worldToLocal;
    return grip;
}

void addSegmentGrips(GripBuildContext& context, const asset::CurveElement& element, const math::Segment3& segment) {
    EditorGrip start = makeGrip(context, element, EditorGripKind::Vertex, EditorGripAction::MoveVertex, segment.start);
    start.vertexIndex = 0;
    context.grips.push_back(std::move(start));

    EditorGrip end = makeGrip(context, element, EditorGripKind::Vertex, EditorGripAction::MoveVertex, segment.end);
    end.vertexIndex = 1;
    context.grips.push_back(std::move(end));

    EditorGrip midpoint =
            makeGrip(context, element, EditorGripKind::Midpoint, EditorGripAction::MovePrimitive, segment.pointAt(0.5));
    context.grips.push_back(std::move(midpoint));
}

void addPolylineGrips(GripBuildContext& context, const asset::CurveElement& element, const math::Polyline3& polyline) {
    for (size_t i = 0; i < polyline.points.size(); ++i) {
        EditorGrip vertex =
                makeGrip(context, element, EditorGripKind::Vertex, EditorGripAction::MoveVertex, polyline.points[i]);
        vertex.vertexIndex = i;
        context.grips.push_back(std::move(vertex));
    }

    for (size_t i = 0; i < polyline.segmentCount(); ++i) {
        const math::Segment3 segment = polyline.segmentAt(i);
        EditorGrip midpoint = makeGrip(context, element, EditorGripKind::Midpoint, EditorGripAction::MoveSegment,
                                       segment.pointAt(0.5));
        midpoint.segmentIndex = i;
        context.grips.push_back(std::move(midpoint));
    }
}

void addCircleGrips(GripBuildContext& context, const asset::CurveElement& element, const math::Circle3& circle) {
    EditorGrip center =
            makeGrip(context, element, EditorGripKind::Center, EditorGripAction::MovePrimitive, circle.center);
    context.grips.push_back(std::move(center));

    const math::Vec3 x = math::perpendicularUnit(circle.normal);
    const math::Vec3 y = circle.normal.cross(x).normalizedOr(math::Vec3::unitY());
    const math::Vec3 directions[] = { x, y, -x, -y };
    for (size_t i = 0; i < 4; ++i) {
        EditorGrip radius = makeGrip(context, element, EditorGripKind::Radius, EditorGripAction::ChangeRadius,
                                     circle.center + directions[i] * circle.radius);
        radius.vertexIndex = i;
        context.grips.push_back(std::move(radius));
    }
}

void addArcGrips(GripBuildContext& context, const asset::CurveElement& element, const math::Arc3& arc) {
    if (!arc.valid() || arc.sweep == math::Angle::zero()) {
        return;
    }

    EditorGrip start =
            makeGrip(context, element, EditorGripKind::Vertex, EditorGripAction::MoveVertex, arc.pointAt(0.0));
    start.vertexIndex = 0;
    context.grips.push_back(std::move(start));

    EditorGrip end = makeGrip(context, element, EditorGripKind::Vertex, EditorGripAction::MoveVertex, arc.pointAt(1.0));
    end.vertexIndex = 1;
    context.grips.push_back(std::move(end));

    EditorGrip center = makeGrip(context, element, EditorGripKind::Center, EditorGripAction::MovePrimitive, arc.center);
    context.grips.push_back(std::move(center));

    EditorGrip radius =
            makeGrip(context, element, EditorGripKind::Radius, EditorGripAction::ChangeRadius, arc.pointAt(0.5));
    context.grips.push_back(std::move(radius));
}

void addCurveElementGrips(GripBuildContext& context, const asset::CurveElement& element) {
    const auto& data = element.primitive.data();
    if (const auto* segment = std::get_if<asset::CurveSegmentPrimitive>(&data)) {
        addSegmentGrips(context, element, segment->segment);
        return;
    }
    if (const auto* polyline = std::get_if<asset::CurvePolylinePrimitive>(&data)) {
        addPolylineGrips(context, element, polyline->polyline);
        return;
    }
    if (const auto* circle = std::get_if<asset::CurveCirclePrimitive>(&data)) {
        addCircleGrips(context, element, circle->circle);
        return;
    }
    if (const auto* arc = std::get_if<asset::CurveArcPrimitive>(&data)) {
        addArcGrips(context, element, arc->arc);
        return;
    }
}

const asset::CurveAsset* selectedCurveAsset(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets) {
        return nullptr;
    }

    const auto* geometry = scene->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return nullptr;
    }

    return dynamic_cast<const asset::CurveAsset*>(assets->asset(geometry->geometry));
}

math::Mat4 entityWorldTransform(const scene::Scene& scene, scene::EntityId entity) {
    if (const auto* transform = scene.transform(entity)) {
        return transform->world;
    }
    return math::Mat4(1.0);
}

}  // namespace

std::vector<EditorGrip> EditorGripProvider::build(const io::Document& document) const {
    std::vector<EditorGrip> grips;
    const scene::Scene* scene = document.scene();
    if (!scene) {
        return grips;
    }

    GripBuildContext context{ .grips = grips };
    scene->forEachEntity([&](scene::EntityId entity) {
        const auto* selection = scene->selection(entity);
        if (!selection || !selection->selected) {
            return;
        }

        const asset::CurveAsset* curve = selectedCurveAsset(document, entity);
        if (!curve) {
            return;
        }

        context.entity = entity;
        context.localToWorld = entityWorldTransform(*scene, entity);
        context.worldToLocal = context.localToWorld.inverse();

        for (const asset::CurveElement& element : curve->elements()) {
            addCurveElementGrips(context, element);
        }
    });

    return grips;
}

}  // namespace mulan::app
