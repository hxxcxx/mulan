#include "editor_input.h"

#include "editor_render_pick_conversion.h"

namespace mulan::editor {

EditorPickQueryWorld::EditorPickQueryWorld(const view::RenderScene* renderScene) : render_scene_(renderScene) {
}

void EditorPickQueryWorld::bind(const view::RenderScene* renderScene) {
    render_scene_ = renderScene;
}

void EditorPickQueryWorld::clear() {
    render_scene_ = nullptr;
}

void EditorPickQueryWorld::collectCandidates(const math::Ray3& ray, double lineToleranceWorld,
                                             std::vector<EditorPickHit>& out) const {
    if (!render_scene_) {
        return;
    }

    std::vector<view::RenderScene::PickResult> picks;
    render_scene_->collectPickCandidates(ray, lineToleranceWorld, picks);
    for (const view::RenderScene::PickResult& pick : picks) {
        EditorPickHit hit = editorPickHitFromRenderPick(pick);
        if (hit.valid()) {
            out.push_back(std::move(hit));
        }
    }
}

}  // namespace mulan::editor
