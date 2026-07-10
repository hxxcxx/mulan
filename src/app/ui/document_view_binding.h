/**
 * @file document_view_binding.h
 * @brief 将 DocumentSession 绑定到 ViewContext，并隐藏内部渲染缓存。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */
#pragma once

#include <optional>

#include "document_pick_bridge.h"
#include "document_render_binding.h"
#include "document_selection_bridge.h"

#include <mulan/engine/render/camera/camera.h>
#include <mulan/scene/entity_id.h>

class DocumentSession;

namespace mulan::view {
class ViewContext;
}

class DocumentViewBinding {
public:
    DocumentViewBinding();
    ~DocumentViewBinding();

    DocumentViewBinding(const DocumentViewBinding&) = delete;
    DocumentViewBinding& operator=(const DocumentViewBinding&) = delete;

    void bind(DocumentSession& session, mulan::view::ViewContext& view);
    void unbind();
    bool isBound() const { return render_binding_.isBound(); }

    void refresh();
    void fitAll();
    void updateCameraClipPlanes();
    const mulan::view::RenderScene* renderScene() const;
    std::optional<mulan::view::RenderScene::PickResult> pickAt(const mulan::engine::Camera& camera, double x, double y);
    std::optional<mulan::view::RenderScene::PickResult> pickEntityAt(const mulan::engine::Camera& camera, double x,
                                                                     double y) {
        return pickAt(camera, x, y);
    }
    bool selectSingle(mulan::scene::EntityId entity);
    bool clearSelection();

private:
    mulan::app::DocumentRenderBinding render_binding_;
    mulan::app::DocumentPickBridge pick_bridge_;
    mulan::app::DocumentSelectionBridge selection_bridge_;
};
