/**
 * @file mesh_picking.cpp
 * @brief 网格拾取实现。
 * @author hxxcxx
 * @date 2026-07-11
 */
#include "mesh_picking.h"

#include <mulan/math/algo/intersect.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace mulan::view::detail {

RaySegmentClosest closestRaySegment(const math::Ray3& ray, const math::Segment3& segment) {
    constexpr double kEps = 1e-15;

    const math::Vec3 rayDir = ray.direction;
    const math::Vec3 segDir = segment.direction();
    const math::Vec3 w0 = ray.origin - segment.start;
    const double a = rayDir.dot(rayDir);
    const double c = segDir.dot(segDir);

    if (a <= kEps) {
        const double t = c > kEps ? std::clamp(segDir.dot(w0) / c, 0.0, 1.0) : 0.0;
        const math::Point3 closest = segment.pointAt(t);
        return RaySegmentClosest{
            .rayT = 0.0,
            .segmentT = t,
            .distanceSq = ray.origin.distanceSq(closest),
            .rayPoint = ray.origin,
            .segmentPoint = closest,
        };
    }

    if (c <= kEps) {
        const double rayT = std::max(0.0, (segment.start - ray.origin).dot(rayDir) / a);
        const math::Point3 rayPoint = ray.pointAt(rayT);
        return RaySegmentClosest{
            .rayT = rayT,
            .segmentT = 0.0,
            .distanceSq = rayPoint.distanceSq(segment.start),
            .rayPoint = rayPoint,
            .segmentPoint = segment.start,
        };
    }

    const double b = rayDir.dot(segDir);
    const double d = rayDir.dot(w0);
    const double e = segDir.dot(w0);
    const double denom = a * c - b * b;

    double segmentT = 0.0;
    if (denom > kEps) {
        segmentT = std::clamp((a * e - b * d) / denom, 0.0, 1.0);
    }

    double rayT = (b * segmentT - d) / a;
    if (rayT < 0.0) {
        rayT = 0.0;
        segmentT = std::clamp(e / c, 0.0, 1.0);
    }

    const math::Point3 rayPoint = ray.pointAt(rayT);
    const math::Point3 segmentPoint = segment.pointAt(segmentT);
    return RaySegmentClosest{
        .rayT = rayT,
        .segmentT = segmentT,
        .distanceSq = rayPoint.distanceSq(segmentPoint),
        .rayPoint = rayPoint,
        .segmentPoint = segmentPoint,
    };
}

bool readPosition(const graphics::Mesh& mesh, uint32_t vertexIndex, math::Point3& out) {
    const auto* position = mesh.layout.find(graphics::VertexSemantic::Position);
    if (!position ||
        (position->format != graphics::VertexFormat::Float3 && position->format != graphics::VertexFormat::Float4)) {
        return false;
    }

    const uint32_t stride = mesh.vertexStride();
    if (stride == 0 || vertexIndex >= mesh.vertexCount()) {
        return false;
    }

    const size_t byteOffset = static_cast<size_t>(vertexIndex) * stride + position->offset;
    if (byteOffset + position->size() > mesh.vertices.size()) {
        return false;
    }

    float data[4]{};
    std::memcpy(data, mesh.vertices.data() + byteOffset, position->size());
    out = math::Point3(static_cast<double>(data[0]), static_cast<double>(data[1]), static_cast<double>(data[2]));
    return true;
}

bool readIndex(const graphics::Mesh& mesh, uint32_t indexIndex, uint32_t& out) {
    const uint32_t indexSize = graphics::indexTypeSize(mesh.indexType);
    const size_t byteOffset = static_cast<size_t>(indexIndex) * indexSize;
    if (indexSize == 0 || byteOffset + indexSize > mesh.indices.size()) {
        return false;
    }

    if (mesh.indexType == graphics::IndexType::UInt16) {
        uint16_t value = 0;
        std::memcpy(&value, mesh.indices.data() + byteOffset, sizeof(value));
        out = value;
        return true;
    }

    uint32_t value = 0;
    std::memcpy(&value, mesh.indices.data() + byteOffset, sizeof(value));
    out = value;
    return true;
}

bool readTriangle(const graphics::Mesh& mesh, uint32_t triangleIndex, math::Point3& v0, math::Point3& v1,
                  math::Point3& v2) {
    if (mesh.topology != graphics::PrimitiveTopology::TriangleList) {
        return false;
    }

    uint32_t i0 = triangleIndex * 3;
    uint32_t i1 = i0 + 1;
    uint32_t i2 = i0 + 2;
    if (!mesh.indices.empty()) {
        if (!readIndex(mesh, i0, i0) || !readIndex(mesh, i1, i1) || !readIndex(mesh, i2, i2)) {
            return false;
        }
    }

    return readPosition(mesh, i0, v0) && readPosition(mesh, i1, v1) && readPosition(mesh, i2, v2);
}

uint32_t lineSegmentCount(const graphics::Mesh& mesh) {
    const uint32_t elementCount = !mesh.indices.empty() ? mesh.indexCount() : mesh.vertexCount();
    if (mesh.topology == graphics::PrimitiveTopology::LineList) {
        return elementCount / 2;
    }
    if (mesh.topology == graphics::PrimitiveTopology::LineStrip) {
        return elementCount > 1 ? elementCount - 1 : 0;
    }
    return 0;
}

bool readLineSegment(const graphics::Mesh& mesh, uint32_t segmentIndex, math::Point3& v0, math::Point3& v1) {
    uint32_t i0 = segmentIndex;
    uint32_t i1 = segmentIndex + 1;
    if (mesh.topology == graphics::PrimitiveTopology::LineList) {
        i0 = segmentIndex * 2;
        i1 = i0 + 1;
    } else if (mesh.topology != graphics::PrimitiveTopology::LineStrip) {
        return false;
    }

    if (!mesh.indices.empty()) {
        if (!readIndex(mesh, i0, i0) || !readIndex(mesh, i1, i1)) {
            return false;
        }
    }

    return readPosition(mesh, i0, v0) && readPosition(mesh, i1, v1);
}

MeshPickResult pickTriangleMesh(const math::Ray3& worldRay, const graphics::Mesh& mesh,
                                const math::Mat4& worldTransform) {
    MeshPickResult result;
    if (mesh.empty() || mesh.topology != graphics::PrimitiveTopology::TriangleList ||
        !mesh.layout.has(graphics::VertexSemantic::Position)) {
        return result;
    }

    const uint32_t triangleCount = !mesh.indices.empty() ? mesh.indexCount() / 3 : mesh.vertexCount() / 3;
    if (triangleCount == 0) {
        return result;
    }

    result.tested = true;
    const math::Ray3 localRay = worldRay.transformed(worldTransform.inverse());

    double bestDistance = std::numeric_limits<double>::max();
    for (uint32_t tri = 0; tri < triangleCount; ++tri) {
        math::Point3 v0;
        math::Point3 v1;
        math::Point3 v2;
        if (!readTriangle(mesh, tri, v0, v1, v2)) {
            continue;
        }

        math::Vec3 barycentric;
        const auto hit = math::intersect(localRay, v0, v1, v2, &barycentric);
        if (!hit.hit) {
            continue;
        }

        const math::Point3 worldPoint = hit.point.transformedBy(worldTransform);
        const double worldDistance = (worldPoint - worldRay.origin).length();
        if (worldDistance < bestDistance) {
            bestDistance = worldDistance;
            result.distance = bestDistance;
            result.kind = RenderScene::PickHitKind::Face;
            result.worldPoint = worldPoint;
            result.hasWorldPoint = true;
            result.worldNormal = (v1 - v0).cross(v2 - v0).transformedAsNormal(worldTransform);
            result.hasWorldNormal = true;
            result.primitiveIndex = tri;
            result.hasPrimitiveIndex = true;
            result.parameter = hit.t;
            result.barycentric = barycentric;
            result.hasBarycentric = true;
        }
    }
    return result;
}

MeshPickResult pickLineMesh(const math::Ray3& worldRay, const graphics::Mesh& mesh, const math::Mat4& worldTransform,
                            double lineToleranceWorld) {
    MeshPickResult result;
    if (mesh.empty() ||
        (mesh.topology != graphics::PrimitiveTopology::LineList &&
         mesh.topology != graphics::PrimitiveTopology::LineStrip) ||
        !mesh.layout.has(graphics::VertexSemantic::Position)) {
        return result;
    }

    const uint32_t segmentCount = lineSegmentCount(mesh);
    if (segmentCount == 0) {
        return result;
    }

    result.tested = true;
    const double toleranceSq = std::max(0.0, lineToleranceWorld) * std::max(0.0, lineToleranceWorld);

    double bestDistance = std::numeric_limits<double>::max();
    for (uint32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        math::Point3 v0;
        math::Point3 v1;
        if (!readLineSegment(mesh, segmentIndex, v0, v1)) {
            continue;
        }

        const math::Segment3 worldSegment(v0.transformedBy(worldTransform), v1.transformedBy(worldTransform));
        const RaySegmentClosest closest = closestRaySegment(worldRay, worldSegment);
        if (closest.distanceSq <= toleranceSq && closest.rayT < bestDistance) {
            bestDistance = closest.rayT;
            result.distance = closest.rayT;
            result.kind = RenderScene::PickHitKind::Edge;
            result.worldPoint = closest.segmentPoint;
            result.hasWorldPoint = true;
            result.worldNormal = worldSegment.direction().normalizedOr(math::Vec3::unitX());
            result.hasWorldNormal = true;
            result.primitiveIndex = segmentIndex;
            result.hasPrimitiveIndex = true;
            result.parameter = closest.segmentT;
            result.toleranceWorld = lineToleranceWorld;
            result.edgeStart = worldSegment.start;
            result.edgeEnd = worldSegment.end;
            result.hasEdgeSegment = true;
        }
    }
    return result;
}

MeshPickResult pickMesh(const math::Ray3& ray, const graphics::Mesh& mesh, const math::Mat4& worldTransform,
                        double lineToleranceWorld) {
    if (mesh.topology == graphics::PrimitiveTopology::TriangleList) {
        return pickTriangleMesh(ray, mesh, worldTransform);
    }

    if (mesh.topology == graphics::PrimitiveTopology::LineList ||
        mesh.topology == graphics::PrimitiveTopology::LineStrip) {
        return pickLineMesh(ray, mesh, worldTransform, lineToleranceWorld);
    }

    return {};
}

void appendTriangleMeshPickCandidates(const math::Ray3& worldRay, const graphics::Mesh& mesh,
                                      const math::Mat4& worldTransform, std::vector<MeshPickResult>& out) {
    if (mesh.empty() || mesh.topology != graphics::PrimitiveTopology::TriangleList ||
        !mesh.layout.has(graphics::VertexSemantic::Position)) {
        return;
    }

    const uint32_t triangleCount = !mesh.indices.empty() ? mesh.indexCount() / 3 : mesh.vertexCount() / 3;
    if (triangleCount == 0) {
        return;
    }

    const math::Ray3 localRay = worldRay.transformed(worldTransform.inverse());
    for (uint32_t tri = 0; tri < triangleCount; ++tri) {
        math::Point3 v0;
        math::Point3 v1;
        math::Point3 v2;
        if (!readTriangle(mesh, tri, v0, v1, v2)) {
            continue;
        }

        math::Vec3 barycentric;
        const auto hit = math::intersect(localRay, v0, v1, v2, &barycentric);
        if (!hit.hit) {
            continue;
        }

        const math::Point3 worldPoint = hit.point.transformedBy(worldTransform);
        out.push_back(MeshPickResult{
                .tested = true,
                .distance = (worldPoint - worldRay.origin).length(),
                .kind = RenderScene::PickHitKind::Face,
                .worldPoint = worldPoint,
                .hasWorldPoint = true,
                .worldNormal = (v1 - v0).cross(v2 - v0).transformedAsNormal(worldTransform),
                .hasWorldNormal = true,
                .primitiveIndex = tri,
                .hasPrimitiveIndex = true,
                .parameter = hit.t,
                .barycentric = barycentric,
                .hasBarycentric = true,
        });
    }
}

void appendLineMeshPickCandidates(const math::Ray3& worldRay, const graphics::Mesh& mesh,
                                  const math::Mat4& worldTransform, double lineToleranceWorld,
                                  std::vector<MeshPickResult>& out) {
    if (mesh.empty() ||
        (mesh.topology != graphics::PrimitiveTopology::LineList &&
         mesh.topology != graphics::PrimitiveTopology::LineStrip) ||
        !mesh.layout.has(graphics::VertexSemantic::Position)) {
        return;
    }

    const uint32_t segmentCount = lineSegmentCount(mesh);
    if (segmentCount == 0) {
        return;
    }

    const double tolerance = std::max(0.0, lineToleranceWorld);
    const double toleranceSq = tolerance * tolerance;
    for (uint32_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        math::Point3 v0;
        math::Point3 v1;
        if (!readLineSegment(mesh, segmentIndex, v0, v1)) {
            continue;
        }

        const math::Segment3 worldSegment(v0.transformedBy(worldTransform), v1.transformedBy(worldTransform));
        const RaySegmentClosest closest = closestRaySegment(worldRay, worldSegment);
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
                .primitiveIndex = segmentIndex,
                .hasPrimitiveIndex = true,
                .parameter = closest.segmentT,
                .toleranceWorld = tolerance,
                .edgeStart = worldSegment.start,
                .edgeEnd = worldSegment.end,
                .hasEdgeSegment = true,
        });
    }
}

void appendMeshPickCandidates(const math::Ray3& ray, const graphics::Mesh& mesh, const math::Mat4& worldTransform,
                              double lineToleranceWorld, std::vector<MeshPickResult>& out) {
    if (mesh.topology == graphics::PrimitiveTopology::TriangleList) {
        appendTriangleMeshPickCandidates(ray, mesh, worldTransform, out);
        return;
    }

    if (mesh.topology == graphics::PrimitiveTopology::LineList ||
        mesh.topology == graphics::PrimitiveTopology::LineStrip) {
        appendLineMeshPickCandidates(ray, mesh, worldTransform, lineToleranceWorld, out);
    }
}

}  // namespace mulan::view::detail
