/**
 * @file render_component.h
 * @brief RenderComponent —— 场景实体的渲染可见性与材质槽引用
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <mulan/asset/asset_id.h>

#include <vector>

namespace mulan::scene {

struct RenderComponent {
    bool visible = true;
    std::vector<asset::AssetId> material_slots;
};

} // namespace mulan::scene
