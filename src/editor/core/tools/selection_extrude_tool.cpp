#include "selection_extrude_tool.h"

#include "../selection/editor_selection.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/io/document.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/scene.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <variant>
#include <vector>

namespace mulan::editor {
namespace {

constexpr double kMinimumExtrudeDistance = 1.0e-9;
constexpr double kPixelsPerProfileExtent = 180.0;
constexpr size_t kCircleSegments = 64;

const asset::GeometryAsset* geometryFor(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets) {
        return nullptr;
    }
    const scene::GeometryComponent* geometry = scene->geometry(entity);
    return geometry && geometry->geometry ? dynamic_cast<const asset::GeometryAsset*>(assets->asset(geometry->geometry))
                                          : nullptr;
}

math::Mat4 worldTransformFor(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    if (const scene::TransformComponent* transform = scene ? scene->transform(entity) : nullptr) {
        return transform->world;
    }
    return math::Mat4{ 1.0 };
}

asset::FaceDefinition transformFace(asset::FaceDefinition face, const math::Mat4& transform) {
    const auto transformLoop = [&transform](asset::FaceLoop& loop) {
        for (math::Point3& point : loop.points) {
            point = point.transformedBy(transform);
        }
    };
    transformLoop(face.outer);
    for (asset::FaceLoop& hole : face.holes) {
        transformLoop(hole);
    }
    face.frame.origin = face.frame.origin.transformedBy(transform);
    face.frame.x = face.frame.x.transformedAsDir(transform).normalizedOr(math::Vec3::unitX());
    face.frame.y = face.frame.y.transformedAsDir(transform).normalizedOr(math::Vec3::unitY());
    face.frame.normal = face.frame.normal.transformedAsDir(transform).normalizedOr(math::Vec3::unitZ());
    return face;
}

std::optional<asset::FaceDefinition> faceFromLoop(std::vector<math::Point3> points) {
    points = asset::cleanFaceLoop(points);
    if (points.size() < 3) {
        return std::nullopt;
    }

    const math::Point3 origin = points.front();
    math::Vec3 normal;
    math::Vec3 x;
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        x = points[i] - origin;
        normal = x.cross(points[i + 1] - origin);
        if (!normal.isZero()) {
            break;
        }
    }
    if (normal.isZero()) {
        return std::nullopt;
    }

    const math::Vec3 unitNormal = normal.normalizedOr(math::Vec3::unitZ());
    const math::Vec3 unitX = x.normalizedOr(math::perpendicularUnit(unitNormal));
    asset::FaceDefinition face{
        .frame =
                asset::FacePlaneFrame{
                        .origin = origin,
                        .x = unitX,
                        .y = unitNormal.cross(unitX).normalizedOr(math::Vec3::unitY()),
                        .normal = unitNormal,
                },
        .outer = asset::FaceLoop{ .points = std::move(points) },
    };
    return asset::buildFaceSolidMesh(face).empty() ? std::nullopt
                                                   : std::optional<asset::FaceDefinition>(std::move(face));
}

std::optional<asset::CurvePrimitive> selectedCurvePrimitive(const asset::CurveAsset& curve,
                                                            const EditorSelectionReference& selection) {
    const auto& elements = curve.elements();
    if (selection.curveElementSelection()) {
        for (const asset::CurveElement& element : elements) {
            if (element.id == selection.subObject.curveElement) {
                return element.primitive;
            }
        }
        return std::nullopt;
    }
    return elements.size() == 1 ? std::optional<asset::CurvePrimitive>(elements.front().primitive) : std::nullopt;
}

std::optional<asset::FaceDefinition> faceFromClosedCurve(const asset::CurvePrimitive& primitive,
                                                         const math::Mat4& transform) {
    if (const auto* polyline = std::get_if<asset::CurvePolylinePrimitive>(&primitive.data())) {
        if (!polyline->polyline.closed) {
            return std::nullopt;
        }
        return faceFromLoop(polyline->polyline.transformed(transform).points);
    }

    const auto* circle = std::get_if<asset::CurveCirclePrimitive>(&primitive.data());
    if (!circle || !circle->circle.valid()) {
        return std::nullopt;
    }
    std::vector<math::Point3> points;
    points.reserve(kCircleSegments);
    for (size_t i = 0; i < kCircleSegments; ++i) {
        const double radians = std::numbers::pi * 2.0 * static_cast<double>(i) / static_cast<double>(kCircleSegments);
        points.push_back(math::pointOnCircle(circle->circle, math::Angle::fromRad(radians)).transformedBy(transform));
    }
    return faceFromLoop(std::move(points));
}

std::optional<math::Circle3> transformedCircleProfile(const asset::CurvePrimitive& primitive,
                                                      const math::Mat4& transform) {
    const auto* circle = std::get_if<asset::CurveCirclePrimitive>(&primitive.data());
    if (!circle || !circle->circle.valid()) {
        return std::nullopt;
    }

    const math::Point3 center = circle->circle.center.transformedBy(transform);
    const math::Vec3 normal = circle->circle.normal.transformedAsDir(transform).normalizedOr(math::Vec3::unitZ());
    const double radius =
            center.distance(math::pointOnCircle(circle->circle, math::Angle::zero()).transformedBy(transform));
    const double perpendicularRadius = center.distance(
            math::pointOnCircle(circle->circle, math::Angle::fromRad(std::numbers::pi * 0.5)).transformedBy(transform));
    const double scale = std::max({ 1.0, radius, perpendicularRadius });
    if (radius <= 1.0e-12 || std::abs(radius - perpendicularRadius) > scale * 1.0e-8) {
        // 非均匀缩放后的圆已成为椭圆，保留离散轮廓以避免错误地建成圆柱。
        return std::nullopt;
    }
    return math::Circle3(center, radius, normal);
}

asset::FaceDefinition offsetFace(asset::FaceDefinition face, const math::Vec3& offset) {
    const auto offsetLoop = [&offset](asset::FaceLoop& loop) {
        for (math::Point3& point : loop.points) {
            point = point + offset;
        }
    };
    offsetLoop(face.outer);
    for (asset::FaceLoop& hole : face.holes) {
        offsetLoop(hole);
    }
    face.frame.origin = face.frame.origin + offset;
    return face;
}

asset::CurvePrimitive closedLoop(const std::vector<math::Point3>& points) {
    return asset::CurvePrimitive::polyline(math::Polyline3(points, true));
}

}  // namespace

std::optional<SelectionExtrudeTool::ExtrudeSource> SelectionExtrudeTool::sourceFromSelection(
        const io::Document& document, const EditorSelectionReference& selection) {
    const asset::GeometryAsset* geometry = geometryFor(document, selection.entity);
    if (!geometry) {
        return std::nullopt;
    }

    const math::Mat4 worldTransform = worldTransformFor(document, selection.entity);
    if (const auto* face = dynamic_cast<const asset::FaceAsset*>(geometry)) {
        asset::FaceDefinition profile = transformFace(face->face(), worldTransform);
        return profile.hasOuterLoop() ? std::optional<ExtrudeSource>(ExtrudeSource{ std::move(profile), true })
                                      : std::nullopt;
    }

    const auto* curve = dynamic_cast<const asset::CurveAsset*>(geometry);
    if (!curve) {
        return std::nullopt;
    }
    const std::optional<asset::CurvePrimitive> primitive = selectedCurvePrimitive(*curve, selection);
    const std::optional<asset::FaceDefinition> profile =
            primitive ? faceFromClosedCurve(*primitive, worldTransform) : std::nullopt;
    return profile ? std::optional<ExtrudeSource>(
                             ExtrudeSource{ *profile, false, transformedCircleProfile(*primitive, worldTransform) })
                   : std::nullopt;
}

EditorAction SelectionExtrudeTool::begin() {
    profile_.reset();
    circle_profile_.reset();
    source_is_face_ = false;
    extrusion_anchor_y_.reset();
    signed_distance_ = 0.0;
    return EditorAction::clearPreview();
}

EditorAction SelectionExtrudeTool::handleInput(const EditorInput& input) {
    if (input.event.isRightPress()) {
        return EditorAction::cancel();
    }
    if (!profile_) {
        return input.event.isLeftPress() ? selectSource(input) : EditorAction::consumeEvent();
    }
    if (input.event.isLeftPress()) {
        return commit();
    }
    if (!input.event.isMouseMove()) {
        return EditorAction::consumeEvent();
    }

    if (!extrusion_anchor_y_) {
        extrusion_anchor_y_ = input.screenY;
    }
    signed_distance_ = signedDistanceFor(input);
    return updatePreview(signed_distance_);
}

EditorAction SelectionExtrudeTool::end(ToolFinishReason reason) {
    if (reason != ToolFinishReason::Finished) {
        return EditorAction::clearPreview();
    }
    return EditorAction::ignored();
}

EditorAction SelectionExtrudeTool::updatePreview(double signedDistance) const {
    if (!profile_) {
        return EditorAction::clearPreview();
    }
    const math::Vec3 offset = profile_->frame.normal * signedDistance;
    const asset::FaceDefinition top = offsetFace(*profile_, offset);

    std::vector<asset::CurvePrimitive> curves;
    const size_t connectorCount =
            source_is_face_ ? profile_->outer.points.size() : std::min<size_t>(4, profile_->outer.points.size());
    curves.reserve(connectorCount + 2);
    curves.push_back(closedLoop(profile_->outer.points));
    curves.push_back(closedLoop(top.outer.points));
    for (size_t i = 0; i < connectorCount; ++i) {
        const size_t index = i * profile_->outer.points.size() / connectorCount;
        curves.push_back(
                asset::CurvePrimitive::segment(math::Segment3(profile_->outer.points[index], top.outer.points[index])));
    }

    std::vector<graphics::Mesh> meshes;
    if (source_is_face_) {
        graphics::Mesh topMesh = asset::buildFaceSolidMesh(top);
        if (!topMesh.empty()) {
            meshes.push_back(std::move(topMesh));
        }
    }
    return EditorAction::setPreview(DraftGeometry::geometry(std::move(curves), std::move(meshes)));
}

EditorAction SelectionExtrudeTool::commit() {
    if (!profile_ || std::abs(signed_distance_) <= kMinimumExtrudeDistance) {
        return EditorAction::consumeEvent();
    }

    modeling::ExtrudeParams params{
        .profile = asset::toPlanarProfile(*profile_),
        .circleProfile = circle_profile_,
        .direction = profile_->frame.normal,
        .distance = std::abs(signed_distance_),
        .inward = signed_distance_ < 0.0,
    };
    return EditorAction::commitAndFinish(DocumentOperation::extrudeFace("Extrude", std::move(params)));
}

double SelectionExtrudeTool::signedDistanceFor(const EditorInput& input) const {
    return (*extrusion_anchor_y_ - input.screenY) * profileScale() / kPixelsPerProfileExtent;
}

double SelectionExtrudeTool::profileScale() const {
    if (!profile_ || profile_->outer.points.empty()) {
        return 1.0;
    }
    math::AABB3 bounds = math::AABB3::empty();
    for (const math::Point3& point : profile_->outer.points) {
        bounds.expand(point);
    }
    return bounds.isEmpty() ? 1.0 : std::max(1.0, (bounds.max - bounds.min).length());
}

EditorAction SelectionExtrudeTool::selectSource(const EditorInput& input) {
    if (!document_ || !input.pickHit || !input.pickHit->valid()) {
        return EditorAction::consumeEvent();
    }
    const EditorSelectionHit selection = makeEditorSelectionHit(*input.pickHit, *document_);
    const std::optional<ExtrudeSource> source = sourceFromSelection(*document_, selection.reference);
    if (!source) {
        return EditorAction::consumeEvent();
    }

    profile_ = source->profile;
    circle_profile_ = source->circleProfile;
    source_is_face_ = source->isFace;
    extrusion_anchor_y_.reset();
    signed_distance_ = 0.0;
    return updatePreview(0.0);
}

}  // namespace mulan::editor
