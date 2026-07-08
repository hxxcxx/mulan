#include "render_scene.h"

#include <mulan/asset/asset_library.h>
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

#include <cstring>
#include <limits>

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
};

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

MeshPickResult pickMesh(const math::Ray3& worldRay, const graphics::Mesh& mesh, const math::Mat4& worldTransform) {
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

        const auto hit = math::intersect(localRay, v0, v1, v2);
        if (!hit.hit) {
            continue;
        }

        const math::Point3 worldPoint = hit.point.transformedBy(worldTransform);
        const double worldDistance = (worldPoint - worldRay.origin).length();
        if (worldDistance < bestDistance) {
            bestDistance = worldDistance;
        }
    }

    if (bestDistance < std::numeric_limits<double>::max()) {
        result.distance = bestDistance;
    }
    return result;
}

MeshPickResult pickMeshAsset(const math::Ray3& ray, const asset::MeshAsset& asset, const math::Mat4& worldTransform) {
    MeshPickResult result;
    for (const auto& primitive : asset.primitives()) {
        const MeshPickResult primitiveHit = pickMesh(ray, primitive.mesh, worldTransform);
        result.tested = result.tested || primitiveHit.tested;
        if (primitiveHit.distance && (!result.distance || *primitiveHit.distance < *result.distance)) {
            result.distance = primitiveHit.distance;
        }
    }
    return result;
}

MeshPickResult pickGeometryAsset(const math::Ray3& ray, const asset::Asset& asset, const math::Mat4& worldTransform) {
    if (const auto* meshAsset = dynamic_cast<const asset::MeshAsset*>(&asset)) {
        return pickMeshAsset(ray, *meshAsset, worldTransform);
    }

    if (const auto* tessellated = dynamic_cast<const asset::TessellatedAsset*>(&asset)) {
        return pickMesh(ray, tessellated->solidMesh(), worldTransform);
    }

    return {};
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

std::optional<RenderScene::PickResult> RenderScene::pick(const math::Ray3& ray) const {
    std::optional<PickResult> best;
    for (const auto& [id, proxy] : proxies_) {
        if (!proxy.visible) {
            continue;
        }

        const auto boundsHit = math::intersect(ray, proxy.worldBounds);
        if (!boundsHit.hit) {
            continue;
        }

        double distance = boundsHit.t;
        if (assets_) {
            const asset::Asset* geometryAsset = assets_->asset(proxy.geometry);
            if (geometryAsset) {
                const MeshPickResult meshHit = pickGeometryAsset(ray, *geometryAsset, proxy.worldTransform);
                if (meshHit.tested) {
                    if (!meshHit.distance) {
                        continue;
                    }
                    distance = *meshHit.distance;
                }
            }
        }

        if (!best || distance < best->distance) {
            best = PickResult{
                .entity = id,
                .pickId = proxy.entity.index(),
                .distance = distance,
            };
        }
    }
    return best;
}

}  // namespace mulan::view
