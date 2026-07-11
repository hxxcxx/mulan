/**
 * @file editor_render_pick_conversion.h
 * @brief 将 View 层拾取结果转换为编辑器拾取语义。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "editor_input.h"

#include <mulan/view/scene_sync/render_scene.h>

namespace mulan::editor {

EditorPickHit editorPickHitFromRenderPick(const view::RenderScene::PickResult& pick);

}  // namespace mulan::editor
