/**
 * @file scene_proxy.h
 * @brief SceneProxy 是 RenderScene 内部使用的轻量实体渲染镜像
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <mulan/asset/asset.h>
#include <mulan/asset/asset_id.h>
#include <mulan/math/math.h>
#include <mulan/render/frontend/pick_identity.h>
#include <mulan/scene/entity_id.h>

#include <cstdint>
#include <vector>

namespace mulan::view {

/// SceneProxy 对下游 RenderWorld 投影可见的精确变化类型。
/// Added/Removed/Geometry/Material 需要重建实体投影；Placement/Visibility
/// 只允许更新对象空间状态，不能重新解析几何和材质资产。
enum class RenderProxyDirty : uint32_t {
    None = 0,
    Added = 1u << 0u,
    Removed = 1u << 1u,
    Placement = 1u << 2u,
    Visibility = 1u << 3u,
    Geometry = 1u << 4u,
    Material = 1u << 5u,
};

constexpr RenderProxyDirty operator|(RenderProxyDirty lhs, RenderProxyDirty rhs) {
    return static_cast<RenderProxyDirty>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr RenderProxyDirty operator&(RenderProxyDirty lhs, RenderProxyDirty rhs) {
    return static_cast<RenderProxyDirty>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr RenderProxyDirty& operator|=(RenderProxyDirty& lhs, RenderProxyDirty rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasAnyRenderProxyDirty(RenderProxyDirty value, RenderProxyDirty mask) {
    return (value & mask) != RenderProxyDirty::None;
}

struct SceneProxy {
    scene::EntityId entity;
    engine::PickId pickId;
    asset::AssetId geometry;
    asset::AssetKind geometryKind = asset::AssetKind::Unknown;
    std::vector<asset::AssetId> materialSlots;
    math::Mat4 worldTransform{ 1.0 };
    math::AABB3 localBounds;
    math::AABB3 worldBounds;
    bool visible = true;
};

}  // namespace mulan::view
