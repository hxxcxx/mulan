/**
 * @file editor_pick_service.h
 * @brief EditorPickService 将视图拾取结果转换为编辑器拾取与选择上下文。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "editor_input.h"
#include "editor_selection.h"

#include <mulan/engine/interaction/input_event.h>
#include <mulan/view/render_scene.h>

#include <optional>

class DocumentSession;
class DocumentViewBinding;

namespace mulan::view {
class ViewContext;
}  // namespace mulan::view

namespace mulan::app {

struct EditorPickInput {
    bool tested = false;
    std::optional<EditorPickHit> hit;
    const view::RenderScene* renderScene = nullptr;
};

class EditorPickService {
public:
    void bind(DocumentSession* session, view::ViewContext* view, DocumentViewBinding* binding);
    void unbind();

    bool isBound() const;
    static bool hasCursorPosition(const engine::InputEvent& event);

    EditorPickInput inputPick(const engine::InputEvent& event) const;
    std::optional<EditorPickHit> pickAtFramebuffer(double screenX, double screenY) const;
    std::optional<EditorSelectionHit> selectionHitAtFramebuffer(double screenX, double screenY) const;
    const view::RenderScene* renderScene() const;

private:
    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    DocumentViewBinding* binding_ = nullptr;
};

EditorPickHit toEditorPickHit(const view::RenderScene::PickResult& pick);

}  // namespace mulan::app
