/**
 * @file render_world_sync.h
 * @brief RenderWorldSync 将 view 的 RenderScene/AssetLibrary 同步到 engine RenderWorld。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <mulan/engine/render/frontend/render_world.h>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class RenderScene;
}

namespace mulan::view {

class RenderWorldSync {
public:
    void rebuild(const RenderScene& scene, const asset::AssetLibrary& assets, engine::RenderWorld& world) const;
};

}  // namespace mulan::view
