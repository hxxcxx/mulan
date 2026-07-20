/**
 * @file document_pick_bridge.h
 * @brief DocumentPickBridge 封装文档视图中的拾取查询。
 *
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/interaction/camera/camera.h>
#include <mulan/view/scene_sync/render_scene.h>

#include <optional>

namespace mulan::editor {

class DocumentRenderBinding;

class DocumentPickBridge {
public:
    void bind(DocumentRenderBinding& renderBinding);
    void unbind();
    bool isBound() const { return render_binding_ != nullptr; }

    std::optional<view::RenderScene::PickResult> pickAt(const engine::Camera& camera, double x, double y);

private:
    DocumentRenderBinding* render_binding_ = nullptr;
};

}  // namespace mulan::editor
