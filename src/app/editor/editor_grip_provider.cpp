#include "editor_grip_provider.h"

#include <mulan/asset/asset_library.h>
#include <mulan/io/document.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/scene.h>

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace mulan::app {
namespace {

struct CurveGripBuildContext {
    std::vector<EditorGrip>& grips;
    scene::EntityId entity = scene::EntityId::invalid();
    math::Mat4 localToWorld = math::Mat4(1.0);
    math::Mat4 worldToLocal = math::Mat4(1.0);
    uint64_t& nextId;
};

struct EmittedCurveElement {
    scene::EntityId entity = scene::EntityId::invalid();
    asset::CurveElementId element = asset::CurveElementId::invalid();
};

EditorGrip makeGrip(CurveGripBuildContext& context, const asset::CurveElement& element, EditorGripKind kind,
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

void addSegmentGrips(CurveGripBuildContext& context, const asset::CurveElement& element,
                     const math::Segment3& segment) {
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

void addPolylineGrips(CurveGripBuildContext& context, const asset::CurveElement& element,
                      const math::Polyline3& polyline) {
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

void addCircleGrips(CurveGripBuildContext& context, const asset::CurveElement& element, const math::Circle3& circle) {
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

void addArcGrips(CurveGripBuildContext& context, const asset::CurveElement& element, const math::Arc3& arc) {
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

void addControlPointGrips(CurveGripBuildContext& context, const asset::CurveElement& element,
                          std::span<const math::Point3> points) {
    for (size_t i = 0; i < points.size(); ++i) {
        EditorGrip controlPoint =
                makeGrip(context, element, EditorGripKind::ControlPoint, EditorGripAction::MoveControlPoint, points[i]);
        controlPoint.vertexIndex = i;
        context.grips.push_back(std::move(controlPoint));
    }
}

void addCurveElementGrips(CurveGripBuildContext& context, const asset::CurveElement& element) {
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
    if (const auto* bezier = std::get_if<asset::CurveBezierPrimitive>(&data)) {
        addControlPointGrips(context, element, bezier->curve.controlPoints());
        return;
    }
    if (const auto* bspline = std::get_if<asset::CurveBSplinePrimitive>(&data)) {
        addControlPointGrips(context, element, bspline->curve.controlPoints());
        return;
    }
    if (const auto* nurbs = std::get_if<asset::CurveNurbsPrimitive>(&data)) {
        addControlPointGrips(context, element, nurbs->curve.controlPoints());
        return;
    }
}

const asset::CurveAsset* curveAssetForEntity(const io::Document& document, scene::EntityId entity) {
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

const asset::CurveElement* findCurveElement(const asset::CurveAsset& curve, asset::CurveElementId id) {
    if (!id.valid()) {
        return nullptr;
    }

    const auto& elements = curve.elements();
    const auto it = std::find_if(elements.begin(), elements.end(),
                                 [id](const asset::CurveElement& element) { return element.id == id; });
    return it != elements.end() ? &*it : nullptr;
}

bool alreadyEmitted(std::span<const EmittedCurveElement> emitted, scene::EntityId entity,
                    asset::CurveElementId element) {
    return std::any_of(emitted.begin(), emitted.end(), [entity, element](const EmittedCurveElement& item) {
        return item.entity == entity && item.element == element;
    });
}

class CurveGripSource final : public EditorGripSource {
public:
    void build(const EditorGripBuildContext& context, std::vector<EditorGrip>& out) const override {
        const scene::Scene* scene = context.document.scene();
        if (!scene) {
            return;
        }

        std::vector<EmittedCurveElement> emitted;
        auto addEntity = [&](scene::EntityId entity, std::optional<asset::CurveElementId> selectedElement) {
            const asset::CurveAsset* curve = curveAssetForEntity(context.document, entity);
            if (!curve) {
                return;
            }

            CurveGripBuildContext curveContext{
                .grips = out,
                .entity = entity,
                .localToWorld = entityWorldTransform(*scene, entity),
                .worldToLocal = entityWorldTransform(*scene, entity).inverse(),
                .nextId = context.nextId,
            };

            if (selectedElement) {
                const asset::CurveElement* element = findCurveElement(*curve, *selectedElement);
                if (!element || alreadyEmitted(emitted, entity, element->id)) {
                    return;
                }
                addCurveElementGrips(curveContext, *element);
                emitted.push_back(EmittedCurveElement{ .entity = entity, .element = element->id });
                return;
            }

            for (const asset::CurveElement& element : curve->elements()) {
                if (alreadyEmitted(emitted, entity, element.id)) {
                    continue;
                }
                addCurveElementGrips(curveContext, element);
                emitted.push_back(EmittedCurveElement{ .entity = entity, .element = element.id });
            }
        };

        for (const EditorSelectionReference& selected : context.selection.selected()) {
            if (!selected.valid()) {
                continue;
            }

            if (selected.domain == EditorSelectionDomain::Curve && selected.subObject.curveElement.valid()) {
                addEntity(selected.entity, selected.subObject.curveElement);
                continue;
            }

            if (selected.domain == EditorSelectionDomain::Entity || selected.wholeEntity()) {
                addEntity(selected.entity, std::nullopt);
            }
        }

        if (!context.selection.empty()) {
            return;
        }

        scene->forEachEntity([&](scene::EntityId entity) {
            const auto* selection = scene->selection(entity);
            if (selection && selection->selected) {
                addEntity(entity, std::nullopt);
            }
        });
    }
};

}  // namespace

EditorGripProvider::EditorGripProvider() {
    addSource(std::make_unique<CurveGripSource>());
}

EditorGripProvider::~EditorGripProvider() = default;

void EditorGripProvider::addSource(std::unique_ptr<EditorGripSource> source) {
    if (source) {
        sources_.push_back(std::move(source));
    }
}

std::vector<EditorGrip> EditorGripProvider::build(const io::Document& document,
                                                  const EditorSelectionContext& selection) const {
    std::vector<EditorGrip> grips;
    uint64_t nextId = 1;
    EditorGripBuildContext context{
        .document = document,
        .selection = selection,
        .nextId = nextId,
    };

    for (const std::unique_ptr<EditorGripSource>& source : sources_) {
        source->build(context, grips);
    }
    return grips;
}

}  // namespace mulan::app
