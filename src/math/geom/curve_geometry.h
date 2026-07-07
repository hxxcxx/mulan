/**
 * @file curve_geometry.h
 * @brief 结构化曲线几何值对象与辅助算法。
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include "../linalg/mat4.h"
#include "../scalar/angle.h"
#include "line.h"
#include "point.h"

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace mulan::math {

struct Polyline3 {
    std::vector<Point3> points;
    bool closed = false;

    Polyline3() = default;
    explicit Polyline3(std::vector<Point3> pts, bool isClosed = false) : points(std::move(pts)), closed(isClosed) {}

    bool empty() const { return points.empty(); }
    size_t size() const { return points.size(); }
    bool hasSegments() const { return points.size() >= 2; }
    size_t segmentCount() const { return hasSegments() ? points.size() - 1 + (closed ? 1 : 0) : 0; }

    Segment3 segmentAt(size_t index) const {
        const size_t next = (index + 1) % points.size();
        return Segment3(points[index], points[next]);
    }

    Polyline3 transformed(const Mat4& m) const {
        Polyline3 result;
        result.closed = closed;
        result.points.reserve(points.size());
        for (const Point3& point : points) {
            result.points.push_back(point.transformedBy(m));
        }
        return result;
    }
};

struct Circle3 {
    Point3 center = Point3::origin();
    Vec3 normal = Vec3::unitZ();
    double radius = 0.0;

    Circle3() = default;
    Circle3(const Point3& c, double r, const Vec3& n = Vec3::unitZ())
        : center(c), normal(n.normalizedOr(Vec3::unitZ())), radius(r) {}

    bool valid() const { return radius > 0.0 && !normal.isZero(); }
};

inline Vec3 rotateAroundAxis(const Vec3& v, const Vec3& axis, Angle angle) {
    const Vec3 n = axis.normalizedOr(Vec3::unitZ());
    const double c = angle.cos();
    const double s = angle.sin();
    return v * c + n.cross(v) * s + n * (n.dot(v) * (1.0 - c));
}

inline Vec3 perpendicularUnit(const Vec3& normal) {
    const Vec3 n = normal.normalizedOr(Vec3::unitZ());
    const Vec3 seed = std::abs(n.z) < 0.9 ? Vec3::unitZ() : Vec3::unitX();
    return seed.cross(n).normalizedOr(Vec3::unitX());
}

inline Point3 pointOnCircle(const Circle3& circle, Angle angle) {
    const Vec3 xAxis = perpendicularUnit(circle.normal);
    return circle.center + rotateAroundAxis(xAxis, circle.normal, angle) * circle.radius;
}

struct Arc3 {
    Point3 center = Point3::origin();
    Vec3 normal = Vec3::unitZ();
    Vec3 startDirection = Vec3::unitX();
    double radius = 0.0;
    Angle sweep = Angle::zero();

    Arc3() = default;
    Arc3(const Point3& c, double r, const Vec3& startDir, Angle sweepAngle, const Vec3& n = Vec3::unitZ())
        : center(c),
          normal(n.normalizedOr(Vec3::unitZ())),
          startDirection(startDir.normalizedOr(Vec3::unitX())),
          radius(r),
          sweep(sweepAngle) {}

    bool valid() const { return radius > 0.0 && !normal.isZero() && !startDirection.isZero(); }

    Point3 pointAt(double t) const { return center + rotateAroundAxis(startDirection, normal, sweep * t) * radius; }
};

}  // namespace mulan::math
