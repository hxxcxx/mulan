/**
 * @file curve_mesh_builder.cpp
 * @brief Builds render-only wire meshes from structured curve primitives.
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "curve_mesh_builder.h"

#include <mulan/graphics/vertex/vertex_buffer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mulan::asset {
namespace {

constexpr int kCircleSegments = 64;
constexpr double kArcStepRadians = math::kPi / 24.0;
constexpr int kMinimumParametricSegments = 32;
constexpr int kMaximumParametricSegments = 256;

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

struct LinePointPair {
    math::Point3 start;
    math::Point3 end;
};

int parametricSegmentCount(int controlPointCount, int multiplier = 16) {
    return std::clamp(controlPointCount * multiplier, kMinimumParametricSegments, kMaximumParametricSegments);
}

void appendSegment(std::vector<LinePointPair>& lines, const math::Segment3& segment) {
    if (segment.lengthSq() <= 0.0) {
        return;
    }
    lines.push_back({ segment.start, segment.end });
}

void appendPolyline(std::vector<LinePointPair>& lines, const math::Polyline3& polyline) {
    const size_t count = polyline.segmentCount();
    for (size_t i = 0; i < count; ++i) {
        appendSegment(lines, polyline.segmentAt(i));
    }
}

void appendCircle(std::vector<LinePointPair>& lines, const math::Circle3& circle) {
    if (!circle.valid()) {
        return;
    }

    math::Point3 previous = math::pointOnCircle(circle, math::Angle::zero());
    for (int i = 1; i <= kCircleSegments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kCircleSegments);
        const math::Point3 current = math::pointOnCircle(circle, math::Angle::fullTurn() * t);
        appendSegment(lines, math::Segment3(previous, current));
        previous = current;
    }
}

void appendArc(std::vector<LinePointPair>& lines, const math::Arc3& arc) {
    if (!arc.valid() || arc.sweep == math::Angle::zero()) {
        return;
    }

    const double radians = std::abs(arc.sweep.radians());
    const int steps = std::max(1, static_cast<int>(std::ceil(radians / kArcStepRadians)));
    math::Point3 previous = arc.pointAt(0.0);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const math::Point3 current = arc.pointAt(t);
        appendSegment(lines, math::Segment3(previous, current));
        previous = current;
    }
}

std::vector<math::Point3> samplePolyline(const math::Polyline3& polyline) {
    std::vector<math::Point3> points = polyline.points;
    if (polyline.closed && points.size() >= 2) {
        points.push_back(points.front());
    }
    return points;
}

std::vector<math::Point3> sampleCircle(const math::Circle3& circle) {
    std::vector<math::Point3> points;
    if (!circle.valid()) {
        return points;
    }

    points.reserve(kCircleSegments + 1);
    for (int i = 0; i <= kCircleSegments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kCircleSegments);
        points.push_back(math::pointOnCircle(circle, math::Angle::fullTurn() * t));
    }
    return points;
}

std::vector<math::Point3> sampleArc(const math::Arc3& arc) {
    std::vector<math::Point3> points;
    if (!arc.valid() || arc.sweep == math::Angle::zero()) {
        return points;
    }

    const double radians = std::abs(arc.sweep.radians());
    const int steps = std::max(1, static_cast<int>(std::ceil(radians / kArcStepRadians)));
    points.reserve(static_cast<size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        points.push_back(arc.pointAt(t));
    }
    return points;
}

std::vector<math::Point3> sampleBezier(const math::BezierCurve3d& curve) {
    const int steps = parametricSegmentCount(curve.controlPointCount());
    std::vector<math::Point3> points;
    points.reserve(static_cast<size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        points.push_back(curve.deCasteljau(t));
    }
    return points;
}

std::vector<math::Point3> sampleBSpline(const math::BSplineCurve3d& curve) {
    const int steps = parametricSegmentCount(curve.controlPointCount());
    const auto [uMin, uMax] = curve.domain();
    std::vector<math::Point3> points;
    points.reserve(static_cast<size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        points.push_back(curve.evaluate(uMin + (uMax - uMin) * t));
    }
    return points;
}

std::vector<math::Point3> sampleNurbs(const math::NURBSCurve3d& curve) {
    const int steps = parametricSegmentCount(curve.controlPointCount());
    const auto [uMin, uMax] = curve.domain();
    std::vector<math::Point3> points;
    points.reserve(static_cast<size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        points.push_back(curve.evaluate(uMin + (uMax - uMin) * t));
    }
    return points;
}

void appendSampledPolyline(std::vector<LinePointPair>& lines, std::span<const math::Point3> points) {
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        appendSegment(lines, math::Segment3(points[i], points[i + 1]));
    }
}

void appendPrimitive(std::vector<LinePointPair>& lines, const CurvePrimitive& primitive) {
    appendSampledPolyline(lines, sampleCurvePrimitive(primitive));
}

graphics::Mesh buildMeshFromLines(std::span<const LinePointPair> lines) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::position3();
    mesh.topology = graphics::PrimitiveTopology::LineList;
    mesh.indexType = graphics::IndexType::UInt32;
    mesh.bounds.reset();

    if (lines.empty()) {
        return mesh;
    }

    graphics::VertexBufferBuilder builder(mesh.layout, static_cast<uint32_t>(lines.size() * 2));
    uint32_t vertex = 0;
    for (const LinePointPair& line : lines) {
        const math::Point3 points[] = { line.start, line.end };
        for (const math::Point3& point : points) {
            builder.setPosition(vertex, static_cast<float>(point.x), static_cast<float>(point.y),
                                static_cast<float>(point.z));
            mesh.bounds.expand(point);
            ++vertex;
        }
    }

    const auto bytes = builder.data();
    mesh.vertices.assign(bytes.begin(), bytes.end());
    return mesh;
}

}  // namespace

std::vector<math::Point3> sampleCurvePrimitive(const CurvePrimitive& primitive) {
    return std::visit(Overloaded{
                              [](const CurveSegmentPrimitive& segment) {
                                  return std::vector<math::Point3>{ segment.segment.start, segment.segment.end };
                              },
                              [](const CurvePolylinePrimitive& polyline) { return samplePolyline(polyline.polyline); },
                              [](const CurveCirclePrimitive& circle) { return sampleCircle(circle.circle); },
                              [](const CurveArcPrimitive& arc) { return sampleArc(arc.arc); },
                              [](const CurveBezierPrimitive& bezier) { return sampleBezier(bezier.curve); },
                              [](const CurveBSplinePrimitive& bspline) { return sampleBSpline(bspline.curve); },
                              [](const CurveNurbsPrimitive& nurbs) { return sampleNurbs(nurbs.curve); },
                      },
                      primitive.data());
}

graphics::Mesh buildCurveWireMesh(std::span<const CurvePrimitive> primitives) {
    std::vector<LinePointPair> lines;
    lines.reserve(primitives.size());
    for (const CurvePrimitive& primitive : primitives) {
        appendPrimitive(lines, primitive);
    }
    return buildMeshFromLines(lines);
}

graphics::Mesh buildCurveWireMesh(std::span<const CurveElement> elements) {
    std::vector<LinePointPair> lines;
    lines.reserve(elements.size());
    for (const CurveElement& element : elements) {
        appendPrimitive(lines, element.primitive);
    }
    return buildMeshFromLines(lines);
}

}  // namespace mulan::asset
