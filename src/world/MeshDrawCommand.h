/**
 * @file MeshDrawCommand.h
 * @brief 完全绑定的 GPU 绘制命令 — Pipeline + Buffers + UBO offsets + instancing
 *
 * 对应 UE5 的 FMeshDrawCommand。
 * 一旦构建完成，GPU 提交时无需查表，直接 execute() 即可。
 *
 * @author hxxcxx
 * @date 2026-06-01
 */

#pragma once

#include "mulan/engine/rhi/PipelineState.h"
#include "mulan/engine/rhi/Buffer.h"
#include "mulan/engine/rhi/CommandList.h"
#include "mulan/engine/rhi/RenderTypes.h"

#include <cstdint>

namespace mulan::world {

struct MeshDrawCommand {
    // Pipeline
    engine::PipelineState* pipelineState = nullptr;

    // Geometry（来自 GpuResourceManager）
    engine::Buffer*  vertexBuffer  = nullptr;
    engine::Buffer*  indexBuffer   = nullptr;
    uint32_t         indexCount    = 0;
    uint32_t         firstIndex    = 0;
    int32_t          baseVertex    = 0;
    uint32_t         vertexCount   = 0;       // non-indexed draw 用
    uint32_t         instanceCount = 1;       // 0 = 不绘制
    engine::PrimitiveTopology topology = engine::PrimitiveTopology::TriangleList;

    // Per-instance UBO offset（instancing 时多个 Entity 共享 command）
    uint32_t         objectUboOffset   = 0;
    uint32_t         materialUboOffset = 0;

    // Sort / Meta
    uint64_t         sortKey     = 0;
    uint32_t         pickId      = 0;
    bool             selected    = false;
    bool             visible     = true;
    bool             isEdge      = false;
    bool             translucent = false;

    /// 提交到 CommandList
    void execute(engine::CommandList& cmd,
                 engine::Buffer* sceneUBO,
                 engine::Buffer* objectUBO,
                 engine::Buffer* materialUBO) const;
};

} // namespace mulan::world
