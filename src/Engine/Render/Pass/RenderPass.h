/**
 * @file RenderPass.h
 * @brief RenderPass 基类 + PassContext
 * @author hxxcxx
 * @date 2026-04-24
 */

#pragma once

#include "../../RHI/Device.h"
#include "../../RHI/CommandList.h"
#include "../../RHI/PipelineState.h"
#include "../../RHI/Buffer.h"
#include "../RenderGeometry.h"

#include <cstdint>

namespace MulanGeo::engine {

struct RenderStats;

// ============================================================
// Pass 执行上下文 — 每帧构造一次，传递给所有 Pass
// ============================================================

struct PassContext {
    RHIDevice*          device     = nullptr;
    CommandList*        cmd        = nullptr;
    const RenderQueue*  queue      = nullptr;

    // UBO buffers
    Buffer*             sceneBuffer    = nullptr;  // b0
    Buffer*             objectBuffer   = nullptr;  // b1
    Buffer*             materialBuffer = nullptr;  // b2

    // Ring buffer 基地址
    uint32_t            sceneOffset    = 0;
    uint32_t            frameBaseIndex = 0;  // m_frameIndex * kMaxDrawCalls

    // PSO
    PipelineState*      solidPso = nullptr;
    PipelineState*      edgePso  = nullptr;

    // 状态 (引用外部)
    uint32_t&           drawCallIndex;
    RenderStats&        stats;

    PassContext(uint32_t& drawCallIndex, RenderStats& stats)
        : drawCallIndex(drawCallIndex), stats(stats) {}
};

// ============================================================
// RenderPass 基类
// ============================================================

class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual void execute(PassContext& ctx) = 0;
    virtual const char* name() const = 0;
};

} // namespace MulanGeo::Engine
