/**
 * @file ForwardPass.h
 * @brief 前向渲染 Pass — 不透明面 + 边线叠加
 * @author hxxcxx
 * @date 2026-04-24
 */

#pragma once

#include "RenderPass.h"

namespace MulanGeo::engine {

class SceneRenderer;

class ForwardPass : public RenderPass {
public:
    explicit ForwardPass(SceneRenderer& renderer);

    void execute(PassContext& ctx) override;
    const char* name() const override { return "ForwardPass"; }

private:
    SceneRenderer& m_renderer;
};

} // namespace MulanGeo::Engine
