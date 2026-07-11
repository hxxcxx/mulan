/**
 * @file document_render_cache.h
 * @brief DocumentRenderCache 维护由当前文档派生出的视图渲染缓存。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/view/scene_sync/render_scene.h>

#include <span>

class DocumentSession;

namespace mulan::editor {

class DocumentRenderCache {
public:
    void clear();
    bool sync(DocumentSession* session);

    const view::RenderScene* renderScene() const;
    const asset::AssetLibrary* assets() const { return assets_; }
    const math::AABB3& sceneBounds() const { return render_scene_.sceneBounds(); }
    const math::Sphere3& sceneBoundsSphere() const { return render_scene_.sceneBoundsSphere(); }
    std::span<const engine::Light> lights() const;

private:
    view::RenderScene render_scene_;
    const asset::AssetLibrary* assets_ = nullptr;
};

}  // namespace mulan::editor
