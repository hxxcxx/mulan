/**
 * @file asset_picking.cpp
 * @brief 资产级拾取与结果转换实现。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "asset_picking.h"

#include "curve_picking.h"
#include "mesh_picking.h"
#include "primitive_pick_index.h"

#include <mulan/asset/curve_asset.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/render/frontend/pick_identity.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <variant>

namespace mulan::view::detail {
namespace {

bool finitePoint(const math::Point3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool finiteVector(const math::Vec3& vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

bool meshHasPickablePrimitives(const graphics::Mesh& mesh) {
    if (mesh.empty() || !mesh.layout.has(graphics::VertexSemantic::Position)) {
        return false;
    }
    if (mesh.topology == graphics::PrimitiveTopology::TriangleList) {
        return (!mesh.indices.empty() ? mesh.indexCount() : mesh.vertexCount()) >= 3;
    }
    if (mesh.topology == graphics::PrimitiveTopology::LineList ||
        mesh.topology == graphics::PrimitiveTopology::LineStrip) {
        return lineSegmentCount(mesh) != 0;
    }
    return false;
}

bool geometryHasPickablePrimitives(std::span<const asset::Drawable> drawables) {
    return std::any_of(drawables.begin(), drawables.end(), [](const asset::Drawable& drawable) {
        return drawable.mesh && meshHasPickablePrimitives(*drawable.mesh);
    });
}

size_t geometryPickablePrimitiveCount(std::span<const asset::Drawable> drawables) {
    size_t count = 0;
    for (const asset::Drawable& drawable : drawables) {
        if (!drawable.mesh || drawable.mesh->empty() ||
            !drawable.mesh->layout.has(graphics::VertexSemantic::Position)) {
            continue;
        }
        if (drawable.mesh->topology == graphics::PrimitiveTopology::TriangleList) {
            count +=
                    (!drawable.mesh->indices.empty() ? drawable.mesh->indexCount() : drawable.mesh->vertexCount()) / 3u;
        } else {
            count += lineSegmentCount(*drawable.mesh);
        }
    }
    return count;
}

struct LocalPickQuery {
    math::Ray3 ray;
    double padding = 0.0;
};

/// 只有有限、非奇异仿射矩阵才可把宽阶段变换到资产本地空间。
/// 线容差乘逆线性部分的 Frobenius 范数，是对任意方向缩放的保守上界。
std::optional<LocalPickQuery> tryMakeLocalQuery(const math::Ray3& worldRay, const math::Mat4& worldTransform,
                                                double lineToleranceWorld) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(worldTransform[column][row])) {
                return std::nullopt;
            }
        }
    }

    // 项目点/方向变换契约只消费 top 3x4，而 Mat4::inverse 会消费完整矩阵。
    // 因而最后一行必须是精确 canonical affine；即使极小 projective 项也可能
    // 与超大平移相乘令完整矩阵奇异，任何容差都会制造宽阶段漏候选。
    if (worldTransform[0].w != 0.0 || worldTransform[1].w != 0.0 || worldTransform[2].w != 0.0 ||
        worldTransform[3].w != 1.0) {
        return std::nullopt;
    }

    constexpr double AffineEpsilon = 64.0 * std::numeric_limits<double>::epsilon();
    const math::Mat3 linear(worldTransform);
    const double columnScale = linear[0].length() * linear[1].length() * linear[2].length();
    const double determinant = linear.determinant();
    if (!(columnScale > 0.0) || !std::isfinite(columnScale) || !std::isfinite(determinant) ||
        std::abs(determinant) <= AffineEpsilon * columnScale) {
        return std::nullopt;
    }

    const math::Mat4 inverse = worldTransform.inverse();
    const math::Mat3 inverseLinear(inverse);
    const math::Point3 localOrigin = worldRay.origin.transformedBy(inverse);
    const math::Vec3 localDirectionRaw = worldRay.direction.transformedAsDir(inverse);
    const double directionLength = localDirectionRaw.length();
    if (!finitePoint(localOrigin) || !finiteVector(localDirectionRaw) || !(directionLength > 0.0) ||
        !std::isfinite(directionLength)) {
        return std::nullopt;
    }

    double inverseNormSq = 0.0;
    for (int column = 0; column < 3; ++column) {
        inverseNormSq += inverseLinear[column].lengthSq();
    }
    const double localPadding = std::max(0.0, lineToleranceWorld) * std::sqrt(inverseNormSq);
    if (!std::isfinite(localPadding)) {
        return std::nullopt;
    }

    return LocalPickQuery{
        .ray = math::Ray3(localOrigin, localDirectionRaw / directionLength),
        .padding = localPadding,
    };
}

bool appendIndexedGeometryCandidates(const math::Ray3& worldRay, const asset::GeometryAsset& geometry,
                                     std::span<const asset::Drawable> drawables, const math::Mat4& worldTransform,
                                     double lineToleranceWorld, std::vector<MeshPickResult>& out,
                                     PrimitivePickIndexCache* indexCache, PrimitivePickQueryStats* indexStats) {
    if (!indexCache) {
        return false;
    }
    const std::optional<LocalPickQuery> localQuery = tryMakeLocalQuery(worldRay, worldTransform, lineToleranceWorld);
    if (!localQuery) {
        return false;
    }
    const PrimitivePickIndex* index = indexCache->get(geometry, drawables);
    if (!index) {
        return false;
    }

    std::vector<PickPrimitiveRef> refs;
    index->queryRay(localQuery->ray, localQuery->padding, refs, indexStats);
    if (indexStats) {
        indexStats->usedIndex = true;
    }
    std::vector<uint32_t> primitiveIndices;
    size_t cursor = 0;
    while (cursor < refs.size()) {
        const uint32_t drawableIndex = refs[cursor].drawableIndex;
        const PickPrimitiveKind kind = refs[cursor].kind;
        primitiveIndices.clear();
        while (cursor < refs.size() && refs[cursor].drawableIndex == drawableIndex && refs[cursor].kind == kind) {
            primitiveIndices.push_back(refs[cursor].primitiveIndex);
            ++cursor;
        }
        if (drawableIndex >= drawables.size() || !drawables[drawableIndex].mesh) {
            continue;
        }

        const size_t first = out.size();
        if (kind == PickPrimitiveKind::Triangle) {
            appendTriangleMeshPickCandidates(worldRay, *drawables[drawableIndex].mesh, worldTransform, primitiveIndices,
                                             out);
        } else {
            appendLineMeshPickCandidates(worldRay, *drawables[drawableIndex].mesh, worldTransform, lineToleranceWorld,
                                         primitiveIndices, out);
        }
        for (size_t resultIndex = first; resultIndex < out.size(); ++resultIndex) {
            out[resultIndex].sourceDrawableIndex = drawableIndex;
        }
    }
    return true;
}

}  // namespace

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
                                 double lineToleranceWorld, PrimitivePickIndexCache* indexCache,
                                 PrimitivePickQueryStats* indexStats) {
    if (indexStats) {
        *indexStats = {};
    }
    if (dynamic_cast<const asset::CurveAsset*>(&asset)) {
        return pickStructuredCurveAsset(ray, asset, worldTransform, lineToleranceWorld);
    }

    const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(&asset);
    if (!geometry) {
        return {};
    }

    std::vector<asset::Drawable> drawables;
    geometry->collectDrawables(drawables);

    if (indexCache) {
        std::vector<MeshPickResult> candidates;
        if (appendIndexedGeometryCandidates(ray, *geometry, drawables, worldTransform, lineToleranceWorld, candidates,
                                            indexCache, indexStats)) {
            MeshPickResult result;
            result.tested = geometryHasPickablePrimitives(drawables);
            for (const MeshPickResult& candidate : candidates) {
                if (candidate.distance && (!result.distance || *candidate.distance < *result.distance)) {
                    result = candidate;
                }
            }
            return result;
        }
        if (indexStats) {
            indexStats->linearFallback = true;
            indexStats->candidatePrimitiveCount = geometryPickablePrimitiveCount(drawables);
        }
    }

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
                                       std::vector<MeshPickResult>& out, PrimitivePickIndexCache* indexCache,
                                       PrimitivePickQueryStats* indexStats) {
    if (indexStats) {
        *indexStats = {};
    }
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

    if (appendIndexedGeometryCandidates(ray, *geometry, drawables, worldTransform, lineToleranceWorld, out, indexCache,
                                        indexStats)) {
        return;
    }
    if (indexCache && indexStats) {
        indexStats->linearFallback = true;
        indexStats->candidatePrimitiveCount = geometryPickablePrimitiveCount(drawables);
    }

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
        .pickId = proxy.pickId,
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
