#include "face_asset.h"

#include <mulan/graphics/vertex/vertex_buffer.h>
#include <mulan/math/algo2d/segment_intersect.h>
#include <mulan/math/algo2d/triangulation.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mulan::asset {
namespace {

constexpr double kMinimumEdgeLengthSq = 1.0e-12;
constexpr double kMinimumArea = 1.0e-10;

constexpr size_t nextLoopIndex(size_t index, size_t size) {
    return (index + 1) % size;
}

struct LinePointPair {
    math::Point3 start;
    math::Point3 end;
};

FacePlaneFrame normalizeFrame(FacePlaneFrame frame) {
    frame.normal = frame.normal.normalizedOr(math::Vec3::unitZ());

    math::Vec3 x = frame.x - frame.normal * frame.x.dot(frame.normal);
    x = x.normalizedOr(math::perpendicularUnit(frame.normal));

    math::Vec3 y = frame.normal.cross(x).normalizedOr(math::Vec3::unitY());
    if (!frame.y.isZero() && y.dot(frame.y) < 0.0) {
        x = -x;
        y = frame.normal.cross(x).normalizedOr(math::Vec3::unitY());
    }

    frame.x = x;
    frame.y = y;
    frame.origin = frame.plane().project(frame.origin);
    return frame;
}

std::vector<math::Point2> projectLoop(const FacePlaneFrame& frame, std::span<const math::Point3> points) {
    std::vector<math::Point2> projected;
    projected.reserve(points.size());
    for (const math::Point3& point : points) {
        projected.push_back(projectToFaceFrame(frame, point));
    }
    return projected;
}

double signedArea(std::span<const math::Point2> points) {
    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i) {
        const math::Point2& a = points[i];
        const math::Point2& b = points[nextLoopIndex(i, points.size())];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

bool adjacentEdges(size_t edgeA, size_t edgeB, size_t edgeCount) {
    return edgeA == edgeB || nextLoopIndex(edgeA, edgeCount) == edgeB || nextLoopIndex(edgeB, edgeCount) == edgeA;
}

bool collinearOverlap(const math::Segment2& a, const math::Segment2& b, const math::Tolerance& tol) {
    const math::Vec2 axis = a.direction();
    const double axisLengthSq = axis.lengthSq();
    if (axisLengthSq <= tol.lengthEps * tol.lengthEps) {
        return false;
    }

    const double crossStart = axis.cross(b.start - a.start);
    const double crossEnd = axis.cross(b.end - a.start);
    const double epsArea = tol.lengthEps * tol.lengthEps;
    if (std::abs(crossStart) > epsArea || std::abs(crossEnd) > epsArea) {
        return false;
    }

    const double t0 = (b.start - a.start).dot(axis) / axisLengthSq;
    const double t1 = (b.end - a.start).dot(axis) / axisLengthSq;
    const double overlapStart = std::max(0.0, std::min(t0, t1));
    const double overlapEnd = std::min(1.0, std::max(t0, t1));
    return overlapEnd - overlapStart > tol.paramEps;
}

FaceLoop cleanLoop(FaceLoop loop) {
    loop.points = cleanFaceLoop(loop.points);
    return loop;
}

FaceDefinition cleanFace(FaceDefinition face) {
    face.frame = normalizeFrame(face.frame);
    face.outer = cleanLoop(std::move(face.outer));

    std::vector<FaceLoop> holes;
    holes.reserve(face.holes.size());
    for (FaceLoop& hole : face.holes) {
        hole = cleanLoop(std::move(hole));
        if (hole.points.size() >= 3) {
            holes.push_back(std::move(hole));
        }
    }
    face.holes = std::move(holes);
    return face;
}

void appendLoopLines(std::vector<LinePointPair>& lines, std::span<const math::Point3> points) {
    if (points.size() < 2) {
        return;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        const math::Point3& start = points[i];
        const math::Point3& end = points[(i + 1) % points.size()];
        if (start.distanceSq(end) > kMinimumEdgeLengthSq) {
            lines.push_back({ start, end });
        }
    }
}

graphics::Mesh buildLineMesh(std::span<const LinePointPair> lines, const math::Vec3& normal) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
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
            builder.setNormal(vertex, static_cast<float>(normal.x), static_cast<float>(normal.y),
                              static_cast<float>(normal.z));
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

}  // namespace

math::Point2 projectToFaceFrame(const FacePlaneFrame& frame, const math::Point3& point) {
    const math::Vec3 delta = frame.plane().project(point) - frame.origin;
    return math::Point2(delta.dot(frame.x), delta.dot(frame.y));
}

math::Point3 pointFromFaceFrame(const FacePlaneFrame& frame, const math::Point2& point) {
    return frame.origin + frame.x * point.x + frame.y * point.y;
}

std::vector<math::Point3> cleanFaceLoop(std::span<const math::Point3> points) {
    std::vector<math::Point3> clean;
    clean.reserve(points.size());
    for (const math::Point3& point : points) {
        if (clean.empty() || clean.back().distanceSq(point) > kMinimumEdgeLengthSq) {
            clean.push_back(point);
        }
    }

    if (clean.size() > 1 && clean.front().distanceSq(clean.back()) <= kMinimumEdgeLengthSq) {
        clean.pop_back();
    }
    return clean;
}

double signedFaceLoopArea(const FacePlaneFrame& frame, std::span<const math::Point3> points) {
    if (points.size() < 3) {
        return 0.0;
    }

    const std::vector<math::Point2> projected = projectLoop(frame, points);
    return signedArea(projected);
}

FaceLoopValidation validateFaceLoop(const FacePlaneFrame& frame, std::span<const math::Point3> points) {
    FaceLoopValidation validation;
    if (points.empty()) {
        validation.status = FaceLoopStatus::Empty;
        return validation;
    }
    if (points.size() < 3) {
        validation.status = FaceLoopStatus::TooFewPoints;
        return validation;
    }

    const std::vector<math::Point2> projected = projectLoop(frame, points);
    if (std::abs(signedArea(projected)) <= kMinimumArea) {
        validation.status = FaceLoopStatus::Degenerate;
        return validation;
    }

    const math::Tolerance tol = math::defaultTolerance();
    for (size_t i = 0; i < projected.size(); ++i) {
        const math::Segment2 edgeA(projected[i], projected[nextLoopIndex(i, projected.size())]);
        for (size_t j = i + 1; j < projected.size(); ++j) {
            if (adjacentEdges(i, j, projected.size())) {
                continue;
            }

            const math::Segment2 edgeB(projected[j], projected[nextLoopIndex(j, projected.size())]);
            if (collinearOverlap(edgeA, edgeB, tol)) {
                validation.status = FaceLoopStatus::Overlapping;
                validation.edgeA = i;
                validation.edgeB = j;
                return validation;
            }

            math::Point2 point;
            if (math::segmentsIntersect(edgeA, edgeB, nullptr, nullptr, &point, tol)) {
                validation.status = FaceLoopStatus::SelfIntersecting;
                validation.edgeA = i;
                validation.edgeB = j;
                validation.point = point;
                validation.hasPoint = true;
                return validation;
            }
        }
    }

    validation.status = FaceLoopStatus::Simple;
    return validation;
}

graphics::Mesh buildFaceSolidMesh(const FaceDefinition& face) {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::TriangleList;
    mesh.indexType = graphics::IndexType::UInt32;
    mesh.bounds.reset();

    const std::vector<math::Point3> points = cleanFaceLoop(face.outer.points);
    if (!validateFaceLoop(face.frame, points).isSimple()) {
        return mesh;
    }

    std::vector<math::Point2> polygon = projectLoop(face.frame, points);
    const math::TriangulationResult triangulation = math::triangulatePolygon(std::move(polygon));
    if (!triangulation.isValid()) {
        return mesh;
    }

    const uint32_t vertexCount = static_cast<uint32_t>(triangulation.triangleCount() * 3);
    graphics::VertexBufferBuilder builder(mesh.layout, vertexCount);

    uint32_t vertex = 0;
    for (const math::Triangle2& triangle : triangulation.triangles) {
        const math::Point2 uvPoints[] = { triangle.v0, triangle.v1, triangle.v2 };
        for (const math::Point2& uvPoint : uvPoints) {
            const math::Point3 world = pointFromFaceFrame(face.frame, uvPoint);
            builder.setPosition(vertex, static_cast<float>(world.x), static_cast<float>(world.y),
                                static_cast<float>(world.z));
            builder.setNormal(vertex, static_cast<float>(face.frame.normal.x), static_cast<float>(face.frame.normal.y),
                              static_cast<float>(face.frame.normal.z));
            const float uv[2] = { static_cast<float>(uvPoint.x), static_cast<float>(uvPoint.y) };
            builder.write(vertex, graphics::VertexSemantic::TexCoord0, uv);
            mesh.bounds.expand(world);
            ++vertex;
        }
    }

    const auto bytes = builder.data();
    mesh.vertices.assign(bytes.begin(), bytes.end());
    return mesh;
}

graphics::Mesh buildFaceWireMesh(const FaceDefinition& face) {
    std::vector<LinePointPair> lines;
    lines.reserve(face.outer.points.size() + face.holes.size() * 4);
    const std::vector<math::Point3> outer = cleanFaceLoop(face.outer.points);
    appendLoopLines(lines, outer);
    for (const FaceLoop& hole : face.holes) {
        const std::vector<math::Point3> holePoints = cleanFaceLoop(hole.points);
        appendLoopLines(lines, holePoints);
    }
    return buildLineMesh(lines, face.frame.normal);
}

FaceRenderMeshes buildFaceRenderMeshes(const FaceDefinition& face) {
    return FaceRenderMeshes{
        .solid = buildFaceSolidMesh(face),
        .wire = buildFaceWireMesh(face),
    };
}

FaceAsset::FaceAsset(AssetId id, std::string name, FaceDefinition face)
    : GeometryAsset(id, AssetKind::Face, std::move(name)) {
    setFace(std::move(face));
}

void FaceAsset::setFace(FaceDefinition face) {
    face_ = cleanFace(std::move(face));
    rebuildRenderMeshes();
}

void FaceAsset::collectDrawables(std::vector<Drawable>& out) const {
    if (!solid_mesh_.empty()) {
        out.push_back({ &solid_mesh_, AssetId::invalid(), DrawableRole::Solid });
    }
    if (!wire_mesh_.empty()) {
        out.push_back({ &wire_mesh_, AssetId::invalid(), DrawableRole::Wire });
    }
}

math::AABB3 FaceAsset::localBounds() const {
    math::AABB3 bounds = math::AABB3::empty();
    if (!solid_mesh_.bounds.isEmpty()) {
        bounds.expand(solid_mesh_.bounds);
    }
    if (!wire_mesh_.bounds.isEmpty()) {
        bounds.expand(wire_mesh_.bounds);
    }
    return bounds;
}

void FaceAsset::rebuildRenderMeshes() {
    FaceRenderMeshes renderMeshes = buildFaceRenderMeshes(face_);
    solid_mesh_ = std::move(renderMeshes.solid);
    wire_mesh_ = std::move(renderMeshes.wire);
}

}  // namespace mulan::asset
