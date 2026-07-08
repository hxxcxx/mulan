#include "render_scene.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/asset/curve_mesh_builder.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/tessellated_asset.h>
#include <mulan/scene/components/bounds_component.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/light_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/entity_dirty.h>
#include <mulan/scene/scene.h>
#include <mulan/math/algo/intersect.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace mulan::view {

namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

engine::Light toRenderLight(const scene::LightComponent& src, const math::Mat4& world) {
    engine::Light dst;
    switch (src.kind) {
    case scene::LightKind::Directional: dst.type = engine::LightType::Directional; break;
    case scene::LightKind::Point: dst.type = engine::LightType::Point; break;
    case scene::LightKind::Spot: dst.type = engine::LightType::Spot; break;
    }

    dst.color = src.color;
    dst.intensity = src.intensity;
    dst.range = src.range;
    dst.innerConeAngle = src.innerConeAngle;
    dst.outerConeAngle = src.outerConeAngle;
    dst.position = math::Point3::origin().transformedBy(world).asVec();
    dst.direction = math::Vec3(0.0, 0.0, -1.0).transformedAsDir(world).normalizedOr(math::Vec3(-0.3, -1.0, -0.4));
    return dst;
}

void collectLights(const scene::Scene& scene, std::vector<engine::Light>& lights) {
    lights.clear();
    scene.forEachEntity([&](scene::EntityId id) {
        if (lights.size() >= engine::LightEnvironment::kMaxLights) {
            return;
        }

        const auto* light = scene.light(id);
        if (!light) {
            return;
        }

        const auto* transform = scene.transform(id);
        const math::Mat4 world = transform ? transform->world : math::Mat4{ 1.0 };
        lights.push_back(toRenderLight(*light, world));
    });
}

struct MeshPickResult {
    bool tested = false;
    std::optional<double> distance;
    RenderScene::PickHitKind kind = RenderScene::PickHitKind::None;
    math::Point3 worldPoint;
    bool hasWorldPoint = false;
    math::Vec3 worldNormal;
    bool hasWorldNormal = false;
    size_t sourceDrawableIndex = 0;
    size_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    double parameter = 0.0;
    double toleranceWorld = 0.0;
    math::Point3 edgeStart;
    math::Point3 edgeEnd;
    bool hasEdgeSegment = false;
    math::Point3 curveCenter;
    math::Vec3 curveNormal;
    double curveRadius = 0.0;
    bool hasCurveCircle = false;
    math::Point3 curveStart;
    math::Point3 curveEnd;
    math::Point3 curveMidpoint;
    bool hasCurveEndpoints = false;
    bool hasCurveMidpoint = false;
    bool curveClosed = false;
    math::Vec3 curveStartDirection;
    double curveSweepRadians = 0.0;
    bool hasCurveRange = false;
    math::Vec3 barycentric;
    bool hasBarycentric = false;
};

struct RaySegmentClosest {
    double rayT = 0.0;
    double segmentT = 0.0;
    double distanceSq = std::numeric_limits<double>::max();
    math::Point3 rayPoint;
    math::Point3 segmentPoint;
};

struct CurveClosestPoint {
    math::Point3 point;
    double parameter = 0.0;
    double distanceToRay = std::numeric_limits<double>::max();
};

math::AABB3 expandedBounds(const math::AABB3& bounds, double amount) {
    if (bounds.isEmpty() || amount <= 0.0) {
        return bounds;
    }

    math::AABB3 expanded = bounds;
    const math::Vec3 pad(amount, amount, amount);
    expanded.min -= pad;
    expanded.max += pad;
    return expanded;
}

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

MeshPickResult pickGeometryAsset(const math::Ray3& ray, const asset::Asset& asset, const math::Mat4& worldTransform,
                                 double lineToleranceWorld) {
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
/// 从 Scene 单个 entity 的组件派生一份 SceneProxy（不参与 bounds 累加）。
/// geometry 缺失时返回 std::nullopt（该 entity 不可渲染）。
/// worldBounds 由 asset 的 localBounds 经 world 矩阵变换重算 —— 这样 transform
/// 变化时 bounds 自动跟随（Scene 层只管 world 矩阵传播，不知 asset）。
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
        .pickId = proxy.entity.index(),
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

std::optional<SceneProxy> buildProxy(const scene::Scene& scene, const asset::AssetLibrary& assets, scene::EntityId id) {
    const auto* geometry = scene.geometry(id);
    if (!geometry || !geometry->geometry) {
        return std::nullopt;
    }

    const asset::Asset* asset = assets.asset(geometry->geometry);
    if (!asset) {
        return std::nullopt;
    }

    // localBounds 仅 GeometryAsset 提供（其他资产类型 dynamic_cast 返回 nullptr）
    const auto* geomAsset = dynamic_cast<const asset::GeometryAsset*>(asset);
    if (!geomAsset) {
        return std::nullopt;
    }

    const auto* render = scene.render(id);
    const auto* selection = scene.selection(id);
    const auto* transform = scene.transform(id);

    SceneProxy proxy;
    proxy.entity = id;
    proxy.geometry = geometry->geometry;
    proxy.geometryKind = asset->kind();
    proxy.materialSlots = render ? render->material_slots : std::vector<asset::AssetId>{};
    proxy.visible = render ? render->visible : true;
    proxy.selected = selection ? selection->selected : false;
    proxy.worldTransform = transform ? transform->world : math::Mat4{ 1.0 };
    // worldBounds = localBounds(资产空间) × world 矩阵
    proxy.worldBounds = geomAsset->localBounds().transformed(proxy.worldTransform);
    return proxy;
}

}  // namespace

// ============================================================
// 全量重建（首次同步 / resetSync 后）
// ============================================================

void RenderScene::sync(scene::Scene& scene, const asset::AssetLibrary& assets) {
    assets_ = &assets;
    collectLights(scene, lights_);

    if (!initialized_) {
        // 全量路径
        proxies_.clear();
        last_sync_stats_ = {};
        scene_bounds_.reset();
        last_sync_stats_.entityCount = scene.entityCount();
        last_sync_stats_.assetCount = assets.count();

        scene.forEachEntity([&](scene::EntityId id) {
            auto proxy = buildProxy(scene, assets, id);
            if (!proxy) {
                ++last_sync_stats_.missingGeometryCount;
                return;
            }
            if (proxy->visible) {
                ++last_sync_stats_.visibleProxyCount;
                scene_bounds_.expand(proxy->worldBounds);
            }
            proxies_[id] = std::move(*proxy);
        });

        last_sync_stats_.proxyCount = proxies_.size();
        // 全量消费所有脏位
        scene.clearDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created |
                         scene::EntityDirty::Destroyed | scene::EntityDirty::Bounds);
        initialized_ = true;
        return;
    }

    // ── 增量路径：只处理自上次同步以来标脏的 entity ──
    last_sync_stats_ = {};
    last_sync_stats_.entityCount = scene.entityCount();
    last_sync_stats_.assetCount = assets.count();

    // 先处理销毁的 entity（proxy 移除）
    scene.forEachDirty(scene::EntityDirty::Destroyed,
                       [&](scene::EntityId id, uint64_t /*flags*/) { proxies_.erase(id); });

    // 处理创建/变更的 entity：重建该 proxy（局部字段更新等价于重建，代码统一）
    // 凡命中 RenderRelated / Created / Bounds 任一位的 entity 都重新派生 proxy。
    scene.forEachDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created | scene::EntityDirty::Bounds,
                       [&](scene::EntityId id, uint64_t flags) {
                           // 已销毁的跳过（Destroyed 已在上面处理，但同一 entity 可能同时带 Created+Destroyed
                           // 的极端时序；isValid 兜底）
                           if (!scene.isValid(id)) {
                               proxies_.erase(id);
                               return;
                           }

                           auto proxy = buildProxy(scene, assets, id);
                           if (!proxy) {
                               // 几何/资产缺失 → 移除已有 proxy（若有）
                               proxies_.erase(id);
                               ++last_sync_stats_.missingGeometryCount;
                               return;
                           }
                           proxies_[id] = std::move(*proxy);
                           (void) flags;
                       });

    // bounds 重新累加（AABB 无逆运算，删除/变更后整体重算 O(n)，远比 proxy map 重建便宜）
    scene_bounds_.reset();
    size_t visibleCount = 0;
    for (const auto& [id, proxy] : proxies_) {
        if (proxy.visible) {
            scene_bounds_.expand(proxy.worldBounds);
            ++visibleCount;
        }
    }
    last_sync_stats_.visibleProxyCount = visibleCount;
    last_sync_stats_.proxyCount = proxies_.size();

    // 清掉本次消费的脏位（Name 等非渲染相关位保留，不影响）
    scene.clearDirty(scene::EntityDirty::RenderRelated | scene::EntityDirty::Created | scene::EntityDirty::Destroyed |
                     scene::EntityDirty::Bounds);
}

void RenderScene::clear() {
    last_sync_stats_ = {};
    scene_bounds_.reset();
    proxies_.clear();
    lights_.clear();
    assets_ = nullptr;
    initialized_ = false;
}

const SceneProxy* RenderScene::proxy(scene::EntityId id) const {
    auto it = proxies_.find(id);
    return it != proxies_.end() ? &it->second : nullptr;
}

void RenderScene::collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld,
                                        std::vector<PickResult>& out) const {
    out.clear();
    if (!assets_) {
        return;
    }

    std::vector<MeshPickResult> meshHits;
    for (const auto& [id, proxy] : proxies_) {
        if (!proxy.visible) {
            continue;
        }

        const math::AABB3 pickBounds = expandedBounds(proxy.worldBounds, lineToleranceWorld);
        const auto boundsHit = math::intersect(ray, pickBounds);
        if (!boundsHit.hit) {
            continue;
        }

        const asset::Asset* geometryAsset = assets_->asset(proxy.geometry);
        if (!geometryAsset) {
            continue;
        }

        meshHits.clear();
        appendGeometryAssetPickCandidates(ray, *geometryAsset, proxy.worldTransform, lineToleranceWorld, meshHits);
        for (const MeshPickResult& meshHit : meshHits) {
            if (!meshHit.distance) {
                continue;
            }
            out.push_back(pickResultFromMeshHit(id, proxy, meshHit, lineToleranceWorld));
        }
    }
}

std::optional<RenderScene::PickResult> RenderScene::pick(const math::Ray3& ray, double lineToleranceWorld) const {
    std::optional<PickResult> best;
    for (const auto& [id, proxy] : proxies_) {
        if (!proxy.visible) {
            continue;
        }

        const math::AABB3 pickBounds = expandedBounds(proxy.worldBounds, lineToleranceWorld);
        const auto boundsHit = math::intersect(ray, pickBounds);
        if (!boundsHit.hit) {
            continue;
        }

        PickResult candidate{
            .entity = id,
            .pickId = proxy.entity.index(),
            .distance = boundsHit.t,
            .kind = PickHitKind::Object,
            .toleranceWorld = lineToleranceWorld,
        };
        if (assets_) {
            const asset::Asset* geometryAsset = assets_->asset(proxy.geometry);
            if (geometryAsset) {
                const MeshPickResult meshHit =
                        pickGeometryAsset(ray, *geometryAsset, proxy.worldTransform, lineToleranceWorld);
                if (meshHit.tested) {
                    if (!meshHit.distance) {
                        continue;
                    }
                    candidate.distance = *meshHit.distance;
                    candidate.kind = meshHit.kind;
                    candidate.worldPoint = meshHit.worldPoint;
                    candidate.hasWorldPoint = meshHit.hasWorldPoint;
                    candidate.worldNormal = meshHit.worldNormal;
                    candidate.hasWorldNormal = meshHit.hasWorldNormal;
                    candidate.sourceDrawableIndex = meshHit.sourceDrawableIndex;
                    candidate.primitiveIndex = meshHit.primitiveIndex;
                    candidate.hasPrimitiveIndex = meshHit.hasPrimitiveIndex;
                    candidate.parameter = meshHit.parameter;
                    candidate.toleranceWorld = meshHit.toleranceWorld;
                    candidate.edgeStart = meshHit.edgeStart;
                    candidate.edgeEnd = meshHit.edgeEnd;
                    candidate.hasEdgeSegment = meshHit.hasEdgeSegment;
                    candidate.curveCenter = meshHit.curveCenter;
                    candidate.curveNormal = meshHit.curveNormal;
                    candidate.curveRadius = meshHit.curveRadius;
                    candidate.hasCurveCircle = meshHit.hasCurveCircle;
                    candidate.curveStart = meshHit.curveStart;
                    candidate.curveEnd = meshHit.curveEnd;
                    candidate.curveMidpoint = meshHit.curveMidpoint;
                    candidate.hasCurveEndpoints = meshHit.hasCurveEndpoints;
                    candidate.hasCurveMidpoint = meshHit.hasCurveMidpoint;
                    candidate.curveClosed = meshHit.curveClosed;
                    candidate.curveStartDirection = meshHit.curveStartDirection;
                    candidate.curveSweepRadians = meshHit.curveSweepRadians;
                    candidate.hasCurveRange = meshHit.hasCurveRange;
                    candidate.barycentric = meshHit.barycentric;
                    candidate.hasBarycentric = meshHit.hasBarycentric;
                }
            }
        }

        if (!best || candidate.distance < best->distance) {
            best = candidate;
        }
    }
    return best;
}

}  // namespace mulan::view
