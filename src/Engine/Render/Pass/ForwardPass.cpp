/**
 * @file ForwardPass.cpp
 * @brief 前向渲染 Pass — 不透明面 + 边线叠加
 * @author hxxcxx
 * @date 2026-04-24
 */

#include "ForwardPass.h"
#include "../SceneRenderer.h"

namespace MulanGeo::engine {

ForwardPass::ForwardPass(SceneRenderer& renderer)
    : m_renderer(renderer) {}

void ForwardPass::execute(PassContext& ctx) {
    // --- 不透明 ---
    if (ctx.solidPso) {
        ctx.cmd->setPipelineState(ctx.solidPso);
        for (const auto& item : ctx.queue->opaqueItems())
            m_renderer.drawItem(item, ctx.cmd, ctx.solidPso, false);
    }

    // --- 边线叠加 ---
    if (ctx.edgePso) {
        ctx.cmd->setPipelineState(ctx.edgePso);
        for (const auto& item : ctx.queue->edgeItems())
            m_renderer.drawItem(item, ctx.cmd, ctx.edgePso, true);
    }

    // --- 半透明 ---
    if (ctx.solidPso) {
        ctx.cmd->setPipelineState(ctx.solidPso);
        for (const auto& item : ctx.queue->transparentItems())
            m_renderer.drawItem(item, ctx.cmd, ctx.solidPso, false);
    }
}

} // namespace MulanGeo::Engine
