/**
 * @file asset_picking.cpp
 * @brief 资产级拾取实现。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "asset_picking.h"

#include "curve_picking.h"
#include "mesh_picking.h"

#include <mulan/asset/curve_asset.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/render/frontend/pick_identity.h>

#include <algorithm>
#include <variant>

namespace mulan::view::detail {

MeshPickResult pickStructuredCurveAsset(const math::Ray3& ray, const asset::Asset& asset,
                                        const math::Mat4& worldTransform, double lineToleranceWorld) {
    std::vector<MeshPickResult> candidates;
    appendGeometryAssetPickCandidates(ray, asset, worldTransform, lineToleranceWorld, candidates);

    MeshPickResult result;
    result.tested = true;
    for (const MeshPickResult& candidate : candidates) {
        if (candidate.distance && (!result.distance || *candidate.distance < *result.distance)) {
            result = candidate;
        }
    }
    return result;
}

MeshPickResult pickGeometryAsset(const math::Ray3& ray, const asset::Asset& asset, const math::Mat4& worldTransform,
                                 double lineToleranceWorld) {
    if (dynamic_cast<const asset::CurveAsset*>(&asset)) {
        return pickStructuredCurveAsset(ray, asset, worldTransform, lineToleranceWorld);
    }

    const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(&asset);
    if (!geometry) {
        return {};
    }

    std::vector<asset::Drawable> drawables;
    geometry->collectDrawables(drawables);

    MeshPickResult result;
    for (size_t drawableIndex = 0; drawableIndex < drawables.size(); ++drawableIndex) {
        const asset::Drawable& drawable = drawables[drawableIndex];
        if (!drawable.mesh) {
            continue;
        }

        const MeshPickResult meshHit = pickMesh(ray, *drawable.mesh, worldTransform, lineToleranceWorld);
        result.tested = result.tested || meshHit.tested;
        if (meshHit.distance && (!result.distance || *meshHit.distance < *result.distance)) {
            result = meshHit;
            result.sourceDrawableIndex = drawableIndex;
        }
    }

    return result;
}

void appendGeometryAssetPickCandidates(const math::Ray3& ray, const asset::Asset& asset,
                                       const math::Mat4& worldTransform, double lineToleranceWorld,
                                       std::vector<MeshPickResult>& out) {
    if (const auto* curve = dynamic_cast<const asset::CurveAsset*>(&asset)) {
        const double tolerance = std::max(0.0, lineToleranceWorld);
        const double toleranceSq = tolerance * tolerance;
        const auto& elements = curve->elements();
        for (size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex) {
            const asset::CurveElement& element = elements[elementIndex];
            std::visit(
                    Overloaded{
                            [&](const asset::CurveSegmentPrimitive& segment) {
                                const math::Segment3 worldSegment = segment.segment.transformed(worldTransform);
                                const RaySegmentClosest closest = closestRaySegment(ray, worldSegment);
                                if (closest.distanceSq > toleranceSq) {
                                    return;
                                }

                                out.push_back(MeshPickResult{
                                        .tested = true,
                                        .distance = closest.rayT,
                                        .kind = RenderScene::PickHitKind::Edge,
                                        .worldPoint = closest.segmentPoint,
                                        .hasWorldPoint = true,
                                        .worldNormal = worldSegment.direction().normalizedOr(math::Vec3::unitX()),
                                        .hasWorldNormal = true,
                                        .sourceDrawableIndex = elementIndex,
                                        .primitiveIndex = static_cast<uint32_t>(elementIndex),
                                        .hasPrimitiveIndex = true,
                                        .parameter = closest.segmentT,
                                        .toleranceWorld = tolerance,
                                        .edgeStart = worldSegment.start,
                                        .edgeEnd = worldSegment.end,
                                        .hasEdgeSegment = true,
                                });
                            },
                            [&](const asset::CurvePolylinePrimitive& polyline) {
                                const math::Polyline3 worldPolyline = polyline.polyline.transformed(worldTransform);
                                for (size_t segmentIndex = 0; segmentIndex < worldPolyline.segmentCount();
                                     ++segmentIndex) {
                                    const math::Segment3 worldSegment = worldPolyline.segmentAt(segmentIndex);
                                    const RaySegmentClosest closest = closestRaySegment(ray, worldSegment);
                                    if (closest.distanceSq > toleranceSq) {
                                        continue;
                                    }

                                    out.push_back(MeshPickResult{
                                            .tested = true,
                                            .distance = closest.rayT,
                                            .kind = RenderScene::PickHitKind::Edge,
                                            .worldPoint = closest.segmentPoint,
                                            .hasWorldPoint = true,
                                            .worldNormal = worldSegment.direction().normalizedOr(math::Vec3::unitX()),
                                            .hasWorldNormal = true,
                                            .sourceDrawableIndex = elementIndex,
                                            .primitiveIndex = static_cast<uint32_t>(elementIndex),
                                            .hasPrimitiveIndex = true,
                                            .parameter = static_cast<double>(segmentIndex) + closest.segmentT,
                                            .toleranceWorld = tolerance,
                                            .edgeStart = worldSegment.start,
                                            .edgeEnd = worldSegment.end,
                                            .hasEdgeSegment = true,
                                    });
                                }
                            },
                            [&](const asset::CurveCirclePrimitive& circlePrimitive) {
                                const math::Circle3 circle = transformCircle(circlePrimitive.circle, worldTransform);
                                const auto closest = closestCirclePointToRay(ray, circle);
                                const bool nearCurve = closest && closest->distanceToRay <= tolerance;
                                const bool nearCenter = math::distance(circle.center, ray) <= tolerance;
                                if (!nearCurve && !nearCenter) {
                                    return;
                                }

                                const math::Point3 nearest = closest ? closest->point : circle.center;
                                out.push_back(MeshPickResult{
                                        .tested = true,
                                        .distance = (nearest - ray.origin).dot(ray.direction),
                                        .kind = RenderScene::PickHitKind::Curve,
                                        .worldPoint = nearest,
                                        .hasWorldPoint = true,
                                        .worldNormal = circle.normal,
                                        .hasWorldNormal = true,
                                        .sourceDrawableIndex = elementIndex,
                                        .primitiveIndex = static_cast<uint32_t>(elementIndex),
                                        .hasPrimitiveIndex = true,
                                        .parameter = closest ? closest->parameter : 0.0,
                                        .toleranceWorld = tolerance,
                                        .curveCenter = circle.center,
                                        .curveNormal = circle.normal,
                                        .curveRadius = circle.radius,
                                        .hasCurveCircle = true,
                                        .curveMidpoint = math::pointOnCircle(circle, math::Angle::halfTurn()),
                                        .hasCurveMidpoint = true,
                                        .curveClosed = true,
                                });
                            },
                            [&](const asset::CurveArcPrimitive& arcPrimitive) {
                                const math::Arc3 arc = transformArc(arcPrimitive.arc, worldTransform);
                                const auto closest = closestArcPointToRay(ray, arc);
                                const bool nearCurve = closest && closest->distanceToRay <= tolerance;
                                const bool nearCenter = math::distance(arc.center, ray) <= tolerance;
                                if (!nearCurve && !nearCenter) {
                                    return;
                                }

                                const math::Point3 nearest = closest ? closest->point : arc.center;
                                out.push_back(MeshPickResult{
                                        .tested = true,
                                        .distance = (nearest - ray.origin).dot(ray.direction),
                                        .kind = RenderScene::PickHitKind::Curve,
                                        .worldPoint = nearest,
                                        .hasWorldPoint = true,
                                        .worldNormal = arc.normal,
                                        .hasWorldNormal = true,
                                        .sourceDrawableIndex = elementIndex,
                                        .primitiveIndex = static_cast<uint32_t>(elementIndex),
                                        .hasPrimitiveIndex = true,
                                        .parameter = closest ? closest->parameter : 0.0,
                                        .toleranceWorld = tolerance,
                                        .curveCenter = arc.center,
                                        .curveNormal = arc.normal,
                                        .curveRadius = arc.radius,
                                        .hasCurveCircle = true,
                                        .curveStart = arc.pointAt(0.0),
                                        .curveEnd = arc.pointAt(1.0),
                                        .curveMidpoint = arc.pointAt(0.5),
                                        .hasCurveEndpoints = true,
                                        .hasCurveMidpoint = true,
                                        .curveStartDirection = arc.startDirection,
                                        .curveSweepRadians = arc.sweep.radians(),
                                        .hasCurveRange = true,
                                });
                            },
                            [&](const asset::CurveBezierPrimitive&) {
                                appendSampledCurvePickCandidate(ray, element.primitive, worldTransform, elementIndex,
                                                                tolerance, out);
                            },
                            [&](const asset::CurveBSplinePrimitive&) {
                                appendSampledCurvePickCandidate(ray, element.primitive, worldTransform, elementIndex,
                                                                tolerance, out);
                            },
                            [&](const asset::CurveNurbsPrimitive&) {
                                appendSampledCurvePickCandidate(ray, element.primitive, worldTransform, elementIndex,
                                                                tolerance, out);
                            },
                    },
                    element.primitive.data());
        }
        return;
    }

    const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(&asset);
    if (!geometry) {
        return;
    }

    std::vector<asset::Drawable> drawables;
    geometry->collectDrawables(drawables);

    for (size_t drawableIndex = 0; drawableIndex < drawables.size(); ++drawableIndex) {
        const asset::Drawable& drawable = drawables[drawableIndex];
        if (!drawable.mesh) {
            continue;
        }

        const size_t first = out.size();
        appendMeshPickCandidates(ray, *drawable.mesh, worldTransform, lineToleranceWorld, out);
        for (size_t i = first; i < out.size(); ++i) {
            out[i].sourceDrawableIndex = drawableIndex;
        }
    }
}

RenderScene::PickResult pickResultFromMeshHit(scene::EntityId id, const SceneProxy& proxy,
                                              const MeshPickResult& meshHit, double lineToleranceWorld) {
    return RenderScene::PickResult{
        .entity = id,
        .pickId = engine::PickId::fromValue(proxy.entity.index()),
        .distance = meshHit.distance.value_or(0.0),
        .kind = meshHit.kind,
        .worldPoint = meshHit.worldPoint,
        .hasWorldPoint = meshHit.hasWorldPoint,
        .worldNormal = meshHit.worldNormal,
        .hasWorldNormal = meshHit.hasWorldNormal,
        .sourceDrawableIndex = meshHit.sourceDrawableIndex,
        .primitiveIndex = meshHit.primitiveIndex,
        .hasPrimitiveIndex = meshHit.hasPrimitiveIndex,
        .parameter = meshHit.parameter,
        .toleranceWorld = meshHit.toleranceWorld > 0.0 ? meshHit.toleranceWorld : lineToleranceWorld,
        .edgeStart = meshHit.edgeStart,
        .edgeEnd = meshHit.edgeEnd,
        .hasEdgeSegment = meshHit.hasEdgeSegment,
        .curveCenter = meshHit.curveCenter,
        .curveNormal = meshHit.curveNormal,
        .curveRadius = meshHit.curveRadius,
        .hasCurveCircle = meshHit.hasCurveCircle,
        .curveStart = meshHit.curveStart,
        .curveEnd = meshHit.curveEnd,
        .curveMidpoint = meshHit.curveMidpoint,
        .hasCurveEndpoints = meshHit.hasCurveEndpoints,
        .hasCurveMidpoint = meshHit.hasCurveMidpoint,
        .curveClosed = meshHit.curveClosed,
        .curveStartDirection = meshHit.curveStartDirection,
        .curveSweepRadians = meshHit.curveSweepRadians,
        .hasCurveRange = meshHit.hasCurveRange,
        .barycentric = meshHit.barycentric,
        .hasBarycentric = meshHit.hasBarycentric,
    };
}

}  // namespace mulan::view::detail
