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

constexpr uint32_t kCurveColor = graphics::packColor(235, 235, 235, 255);
constexpr int kCircleSegments = 64;
constexpr double kArcStepRadians = math::kPi / 24.0;

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

void appendPrimitive(std::vector<LinePointPair>& lines, const CurvePrimitive& primitive) {
    std::visit(Overloaded{
                       [&lines](const CurveSegmentPrimitive& segment) { appendSegment(lines, segment.segment); },
                       [&lines](const CurvePolylinePrimitive& polyline) { appendPolyline(lines, polyline.polyline); },
                       [&lines](const CurveCirclePrimitive& circle) { appendCircle(lines, circle.circle); },
                       [&lines](const CurveArcPrimitive& arc) { appendArc(lines, arc.arc); },
               },
               primitive.data());
}

graphics::Mesh buildMeshFromLines(std::span<const LinePointPair> lines) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::wire();
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
            builder.setColor(vertex, kCurveColor);
            mesh.bounds.expand(point);
            ++vertex;
        }
    }

    const auto bytes = builder.data();
    mesh.vertices.assign(bytes.begin(), bytes.end());
    return mesh;
}

}  // namespace

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
