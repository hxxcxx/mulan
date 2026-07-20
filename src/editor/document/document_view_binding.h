/**
 * @file document_view_binding.h
 * @brief 将 DocumentSession 绑定到 ViewContext，并隐藏内部渲染缓存。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */
#pragma once

#include <functional>
#include <optional>

#include "document_pick_bridge.h"
#include "document_render_binding.h"
#include "document_selection_bridge.h"

#include <mulan/interaction/camera/camera.h>
#include <mulan/scene/entity_id.h>

namespace mulan::view {
class ViewContext;
}

namespace mulan::editor {

class DocumentSession;

class DocumentViewBinding {
public:
    DocumentViewBinding();
    ~DocumentViewBinding();

    DocumentViewBinding(const DocumentViewBinding&) = delete;
    DocumentViewBinding& operator=(const DocumentViewBinding&) = delete;

    void bind(DocumentSession& session, mulan::view::ViewContext& view);
    void unbind();
    bool isBound() const { return render_binding_.isBound(); }

    /// 设置统一帧失效回调；DocumentRenderBinding 不再深层直接渲染。
    void setFrameInvalidationCallback(std::function<void()> callback);

    void refresh();
    void fitAll();
    void prepareFrame(ClipUpdateMode mode = ClipUpdateMode::Settled);
    const mulan::view::RenderScene* renderScene() const;
    std::optional<mulan::view::RenderScene::PickResult> pickAt(const mulan::engine::Camera& camera, double x, double y);
    std::optional<mulan::view::RenderScene::PickResult> pickEntityAt(const mulan::engine::Camera& camera, double x,
                                                                     double y) {
        return pickAt(camera, x, y);
    }
    bool selectSingle(mulan::scene::EntityId entity);
    bool clearSelection();

private:
    DocumentRenderBinding render_binding_;
    DocumentPickBridge pick_bridge_;
    DocumentSelectionBridge selection_bridge_;
};

}  // namespace mulan::editor
