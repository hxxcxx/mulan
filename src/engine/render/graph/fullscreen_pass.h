/**
 * @file fullscreen_pass.h
 * @brief 全屏三角形 pass — 用于 IBL 烘焙等离屏后处理
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 提供无顶点缓冲的 fullscreen-triangle 渲染：VS 用 SV_VertexID 生成覆盖
 * 整个屏幕的三角形。调用方负责创建 PSO（vs=ibl.vert + 自选 frag）和 BindGroup，
 * 通过 blit() 录制一次 beginRenderPass → draw(3) → endRenderPass。
 */

#pragma once

#include "../../rhi/command_list.h"
#include "../../rhi/device.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/render_types.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/texture.h"

#include <cstdint>

namespace mulan::engine {

/// 渲染一个全屏三角形到 colorTarget。viewport/scissor 自动按 w/h 设置。
/// 用于 IBL 烘焙等离屏 fullscreen pass（colorTarget 是普通 2D 纹理，mip/face 参数保留兼容但未使用）。
inline void blitToSlice(CommandList& cmd, PipelineState& pso, BindGroup& bg,
                        Texture& colorTarget, TextureFormat /*targetFormat*/,
                        uint32_t /*mipLevel*/, uint32_t /*arrayLayer*/,
                        uint32_t width, uint32_t height,
                        bool clear = true) {
    RenderPassBeginInfo rp;
    rp.colorAttachments[0].target      = &colorTarget;
    rp.colorAttachments[0].loadAction  = clear ? LoadAction::Clear : LoadAction::Load;
    rp.colorAttachments[0].storeAction = StoreAction::Store;
    rp.colorCount  = 1;
    rp.width       = width;
    rp.height      = height;
    rp.clearColor[0] = 0.f;
    rp.clearColor[1] = 0.f;
    rp.clearColor[2] = 0.f;
    rp.clearColor[3] = 1.f;

    cmd.beginRenderPass(rp);

    Viewport vp;
    vp.x = 0.f; vp.y = 0.f;
    vp.width  = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    cmd.setViewport(vp);

    ScissorRect sr;
    sr.x = 0; sr.y = 0;
    sr.width  = static_cast<int32_t>(width);
    sr.height = static_cast<int32_t>(height);
    cmd.setScissorRect(sr);

    cmd.setPipelineState(&pso);
    cmd.bindGroup(bg);

    DrawAttribs attr;
    attr.vertexCount = 3;
    attr.instanceCount = 1;
    cmd.draw(attr);

    cmd.endRenderPass();
}

} // namespace mulan::engine
