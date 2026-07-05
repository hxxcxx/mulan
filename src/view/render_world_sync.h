#pragma once

#include <mulan/engine/render/frontend/render_world.h>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::render_scene {
class RenderScene;
}

namespace mulan::view {

class RenderWorldSync {
public:
    void rebuild(const render_scene::RenderScene& scene,
                 const asset::AssetLibrary& assets,
                 engine::RenderWorld& world) const;
};

} // namespace mulan::view
