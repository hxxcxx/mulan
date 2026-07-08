#include "control_polygon_builder.h"

#include <mulan/graphics/vertex/vertex_buffer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mulan::app {
namespace {

constexpr int kDiskSegments = 24;

void appendControlPolygon(std::vector<asset::CurvePrimitive>& curves, std::span<const math::Point3> points) {
    if (points.size() < 2) {
        return;
    }
    curves.push_back(asset::CurvePrimitive::polyline(
            math::Polyline3(std::vector<math::Point3>(points.begin(), points.end()), false)));
}

}  // namespace

ControlMarkerBasis controlMarkerBasisFromNormal(const math::Vec3& normal) {
    const math::Vec3 n = normal.normalizedOr(math::Vec3::unitZ());
    const math::Vec3 seed = std::abs(n.z) < 0.9 ? math::Vec3::unitZ() : math::Vec3::unitY();
    const math::Vec3 x = seed.cross(n).normalizedOr(math::Vec3::unitX());
    const math::Vec3 y = n.cross(x).normalizedOr(math::Vec3::unitY());
    return ControlMarkerBasis{ .x = x, .y = y, .normal = n };
}

ControlMarkerBasis controlMarkerBasisFromCamera(const engine::Camera& camera) {
    const math::Vec3 x = camera.right().normalizedOr(math::Vec3::unitX());
    const math::Vec3 y = camera.up().normalizedOr(math::Vec3::unitY());
    const math::Vec3 n = x.cross(y).normalizedOr(camera.forward());
    return ControlMarkerBasis{ .x = x, .y = y, .normal = n };
}

double controlMarkerWorldSize(const engine::Camera& camera, const math::Point3& point, double pixels) {
    const double safePixels = std::max(1.0, pixels);
    const double viewportHeight = static_cast<double>(std::max(1, camera.height()));
    if (camera.isOrthographic()) {
        return safePixels * (2.0 * camera.orthoSize()) / viewportHeight;
    }

    const double depth = std::max(camera.nearPlane(), (point.asVec() - camera.eyePosition()).dot(camera.forward()));
    const double viewHeightAtPoint = 2.0 * depth * std::tan(camera.fieldOfView() * 0.5);
    return safePixels * viewHeightAtPoint / viewportHeight;
}

graphics::Mesh buildControlPointDisk(const math::Point3& center, const ControlMarkerBasis& basis, double radius) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::TriangleList;
    mesh.indexType = graphics::IndexType::UInt32;
    mesh.bounds.reset();

    if (radius <= 0.0) {
        return mesh;
    }

    const uint32_t vertexCount = static_cast<uint32_t>(kDiskSegments * 3);
    graphics::VertexBufferBuilder builder(mesh.layout, vertexCount);
    uint32_t vertex = 0;
    for (int i = 0; i < kDiskSegments; ++i) {
        const double a0 = math::kPi2 * static_cast<double>(i) / static_cast<double>(kDiskSegments);
        const double a1 = math::kPi2 * static_cast<double>(i + 1) / static_cast<double>(kDiskSegments);
        const math::Point3 points[] = {
            center,
            center + basis.x * (std::cos(a0) * radius) + basis.y * (std::sin(a0) * radius),
            center + basis.x * (std::cos(a1) * radius) + basis.y * (std::sin(a1) * radius),
        };

        for (const math::Point3& point : points) {
            builder.setPosition(vertex, static_cast<float>(point.x), static_cast<float>(point.y),
                                static_cast<float>(point.z));
            builder.setNormal(vertex, static_cast<float>(basis.normal.x), static_cast<float>(basis.normal.y),
                              static_cast<float>(basis.normal.z));
            const float uv[2] = { 0.0f, 0.0f };
            builder.write(vertex, graphics::VertexSemantic::TexCoord0, uv);
            mesh.bounds.expand(point);
            ++vertex;
        }
    }

    const auto bytes = builder.data();
    mesh.vertices.assign(bytes.begin(), bytes.end());
    return mesh;
}

DraftGeometry buildControlPolygonGeometry(std::span<const math::Point3> points, const ControlMarkerBasis& basis,
                                          double markerRadius) {
    std::vector<asset::CurvePrimitive> curves;
    std::vector<graphics::Mesh> meshes;
    curves.reserve(points.size() > 1 ? 1 : 0);
    meshes.reserve(points.size());

    appendControlPolygon(curves, points);
    for (const math::Point3& point : points) {
        graphics::Mesh marker = buildControlPointDisk(point, basis, markerRadius);
        if (!marker.empty()) {
            meshes.push_back(std::move(marker));
        }
    }

    return DraftGeometry::geometry(std::move(curves), std::move(meshes));
}

}  // namespace mulan::app
