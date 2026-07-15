/**
 * @file geometry_query.h
 * @brief GeometryQueryWorld 封装 RenderScene 的几何拾取查询。
 * @author hxxcxx
 * @date 2026-07-09 (原始) / 2026-07-15 (BVH 宽阶段接入)
 */
#pragma once

#include <mulan/view/scene_sync/render_scene.h>

namespace mulan::view {

class GeometryQueryWorld {
public:
    explicit GeometryQueryWorld(const RenderScene& scene);

    std::optional<RenderScene::PickResult> pick(const math::Ray3& ray, double lineToleranceWorld = 0.0,
                                                PickQueryStats* stats = nullptr) const;
    void collectPickCandidates(const math::Ray3& ray, double lineToleranceWorld,
                               std::vector<RenderScene::PickResult>& out, PickQueryStats* stats = nullptr) const;

private:
    const RenderScene* scene_ = nullptr;
    const asset::AssetLibrary* assets_ = nullptr;
};

}  // namespace mulan::view
