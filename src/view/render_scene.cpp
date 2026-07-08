#include "render_scene.h"

#include <mulan/asset/asset_library.h>
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
