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
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <vector>

namespace mulan::render_scene {

struct SceneProxy {
    scene::EntityId entity;
    asset::AssetId geometry;
    asset::AssetKind geometryKind = asset::AssetKind::Unknown;
    std::vector<asset::AssetId> materialSlots;
    math::Mat4 worldTransform{1.0};
    math::AABB3 worldBounds;
    bool visible = true;
    bool selected = false;
};

} // namespace mulan::render_scene
