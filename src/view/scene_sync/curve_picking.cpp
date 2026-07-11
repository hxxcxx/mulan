/**
 * @file curve_picking.cpp
 * @brief 曲线拾取实现。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "curve_picking.h"

#include <mulan/asset/curve_mesh_builder.h>
#include <mulan/math/algo/intersect.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace mulan::view::detail {

math::Circle3 transformCircle(const math::Circle3& circle, const math::Mat4& transform) {
    const math::Point3 center = circle.center.transformedBy(transform);
    const math::Point3 radiusPoint = math::pointOnCircle(circle, math::Angle::zero()).transformedBy(transform);
    return math::Circle3(center, center.distance(radiusPoint), circle.normal.transformedAsNormal(transform));
}

math::Arc3 transformArc(const math::Arc3& arc, const math::Mat4& transform) {
    const math::Point3 center = arc.center.transformedBy(transform);
    const math::Point3 start = arc.pointAt(0.0).transformedBy(transform);
    return math::Arc3(center, center.distance(start), (start - center).normalizedOr(math::Vec3::unitX()), arc.sweep,
                      arc.normal.transformedAsNormal(transform));
}

double signedAngleAround(const math::Vec3& from, const math::Vec3& to, const math::Vec3& normal) {
    const math::Vec3 a = from.normalizedOr(math::Vec3::unitX());
    const math::Vec3 b = to.normalizedOr(a);
    const math::Vec3 n = normal.normalizedOr(math::Vec3::unitZ());
    return std::atan2(n.dot(a.cross(b)), a.dot(b));
}

double normalizedCircleParameter(const math::Vec3& direction, const math::Vec3& normal) {
    const math::Vec3 x = math::perpendicularUnit(normal);
    double angle = signedAngleAround(x, direction, normal);
    if (angle < 0.0) {
        angle += math::kPi2;
    }
    return angle / math::kPi2;
}

double clampedArcParameterForDirection(const math::Arc3& arc, const math::Vec3& direction) {
    const double sweep = arc.sweep.radians();
    if (std::abs(sweep) <= 1.0e-12) {
        return 0.0;
    }

    double angle = signedAngleAround(arc.startDirection, direction, arc.normal);
    if (sweep > 0.0) {
        while (angle < 0.0) {
            angle += math::kPi2;
        }
        angle = std::clamp(angle, 0.0, sweep);
    } else {
        while (angle > 0.0) {
            angle -= math::kPi2;
        }
        angle = std::clamp(angle, sweep, 0.0);
    }
    return std::clamp(angle / sweep, 0.0, 1.0);
}

std::optional<CurveClosestPoint> closestCirclePointToRay(const math::Ray3& ray, const math::Circle3& circle) {
    if (!circle.valid()) {
        return std::nullopt;
    }

    const math::Plane3 plane = math::Plane3::fromPointNormal(circle.center, circle.normal);
    const auto planeHit = math::intersect(ray, plane);
    if (!planeHit.hit) {
        return std::nullopt;
    }

    math::Vec3 radial = plane.project(planeHit.point) - circle.center;
    radial -= circle.normal * radial.dot(circle.normal);
    radial = radial.normalizedOr(math::perpendicularUnit(circle.normal));
    const math::Point3 point = circle.center + radial * circle.radius;
    return CurveClosestPoint{
        .point = point,
        .parameter = normalizedCircleParameter(radial, circle.normal),
        .distanceToRay = math::distance(point, ray),
    };
}

std::optional<CurveClosestPoint> closestArcPointToRay(const math::Ray3& ray, const math::Arc3& arc) {
    if (!arc.valid() || arc.sweep == math::Angle::zero()) {
        return std::nullopt;
    }

    const math::Plane3 plane = math::Plane3::fromPointNormal(arc.center, arc.normal);
    const auto planeHit = math::intersect(ray, plane);
    if (!planeHit.hit) {
        return std::nullopt;
    }

    math::Vec3 radial = plane.project(planeHit.point) - arc.center;
    radial -= arc.normal * radial.dot(arc.normal);
    radial = radial.normalizedOr(arc.startDirection);
    const double parameter = clampedArcParameterForDirection(arc, radial);
    const math::Point3 point = arc.pointAt(parameter);
    return CurveClosestPoint{
        .point = point,
        .parameter = parameter,
        .distanceToRay = math::distance(point, ray),
    };
}

void appendSampledCurvePickCandidate(const math::Ray3& ray, const asset::CurvePrimitive& primitive,
                                     const math::Mat4& worldTransform, size_t elementIndex, double tolerance,
                                     std::vector<MeshPickResult>& out) {
    std::vector<math::Point3> samples = asset::sampleCurvePrimitive(primitive);
    if (samples.size() < 2) {
        return;
    }

    for (math::Point3& point : samples) {
        point = point.transformedBy(worldTransform);
    }

    const double toleranceSq = tolerance * tolerance;
    std::optional<RaySegmentClosest> bestClosest;
    size_t bestSegment = 0;
    for (size_t i = 0; i + 1 < samples.size(); ++i) {
        const math::Segment3 segment(samples[i], samples[i + 1]);
        const RaySegmentClosest closest = closestRaySegment(ray, segment);
        if (closest.distanceSq > toleranceSq) {
            continue;
        }
        if (!bestClosest || closest.rayT < bestClosest->rayT) {
            bestClosest = closest;
            bestSegment = i;
        }
    }

    if (!bestClosest) {
        return;
    }

    const double segmentCount = static_cast<double>(samples.size() - 1);
    const double parameter = (static_cast<double>(bestSegment) + bestClosest->segmentT) / segmentCount;
    const math::Point3 midpoint = samples[samples.size() / 2];
    out.push_back(MeshPickResult{
            .tested = true,
            .distance = bestClosest->rayT,
            .kind = RenderScene::PickHitKind::Curve,
            .worldPoint = bestClosest->segmentPoint,
            .hasWorldPoint = true,
            .worldNormal = (samples[bestSegment + 1] - samples[bestSegment]).normalizedOr(math::Vec3::unitX()),
            .hasWorldNormal = true,
            .sourceDrawableIndex = elementIndex,
            .primitiveIndex = static_cast<uint32_t>(elementIndex),
            .hasPrimitiveIndex = true,
            .parameter = parameter,
            .toleranceWorld = tolerance,
            .curveStart = samples.front(),
            .curveEnd = samples.back(),
            .curveMidpoint = midpoint,
            .hasCurveEndpoints = true,
            .hasCurveMidpoint = true,
            .curveClosed = samples.front().distanceSq(samples.back()) <= 1.0e-12,
    });
}

}  // namespace mulan::view::detail
