/**
 * @file mesh_draw_command.h
 * @brief 完全绑定的 GPU 绘制命令 — Pipeline + Buffers + UBO offsets + instancing
 *
 * 对应 UE5 的 FMeshDrawCommand。
 * 一旦构建完成，GPU 提交时无需查表，直接 execute() 即可。
 *
 * @author hxxcxx
 * @date 2026-06-01
 */

#pragma once

#include "../rhi/pipeline_state.h"
#include "../rhi/buffer.h"
#include "../rhi/render_types.h"
#include "../rhi/command_list.h"
#include "../rhi/texture.h"
#include "../rhi/sampler.h"
#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::engine {

struct MeshDrawCommand {
    // Pipeline
    PipelineState* pipelineState = nullptr;

    // Geometry（来自 RenderResourceCache）
    Buffer*  vertexBuffer  = nullptr;
    Buffer*  indexBuffer   = nullptr;
    uint32_t indexCount    = 0;
    uint32_t firstIndex    = 0;
    int32_t  baseVertex    = 0;
    uint32_t vertexCount   = 0;       // non-indexed draw 用
    uint32_t instanceCount = 1;       // 0 = 不绘制
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    // Per-instance UBO offset（instancing 时多个 Entity 共享 command）
    uint32_t objectUboOffset   = 0;
    uint32_t materialUboOffset = 0;

    // 纹理（binding=3 albedo / binding=4 sampler）。
    // 由 DrawCommandBuilder 解析材质填充。albedoTex 为空时由 execute 用 defaultWhite 退化。
    Texture* albedoTex = nullptr;
    Sampler* sampler   = nullptr;

    // Per-object data（Pass::execute 时写入 objectUBO）
    math::Mat4 worldTransform{1.0f};

    // Sort / Meta
    uint64_t sortKey     = 0;
    uint32_t pickId      = 0;
    bool     selected    = false;
    bool     visible     = true;
    bool     isWire      = false;
    bool     translucent = false;

    /// 提交到 CommandList。
    /// defaultWhite / defaultSampler：仅对声明了纹理 binding 的 PSO 有效；
    /// 若 albedoTex/sampler 为空，则用这俩默认值退化（保证无材质模型视觉不变）。
    void execute(CommandList& cmd,
                 Buffer* sceneUBO,
                 Buffer* objectUBO,
                 Buffer* materialUBO,
                 Texture* defaultWhite = nullptr,
                 Sampler* defaultSampler = nullptr) const;

    /// Object UBO slot 步进（字节）。
    /// 单条 ObjectUniforms 记录为 128 字节，但 D3D12 要求 root CBV 偏移
    /// 256 字节对齐（D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT），
    /// Vulkan 的 minUniformBufferOffsetAlignment 在 NVIDIA 上亦为 256。
    /// 故 slot 步进取 256，尾随 128 字节 padding（shader 不读取），
    /// 保证两个后端的多 object offset 都合法，避免 device removed。
    static constexpr uint32_t kObjectUboStride = 256;
};

} // namespace mulan::engine
