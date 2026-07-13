/**
 * @file mesh_draw_command.h
 * @brief 已编译 GPU 绘制命令 — Pipeline + Buffers + UBO offsets + instancing
 *
 * 对应 UE5 的 FMeshDrawCommand。
 * 一旦由 backend compiler 构建完成，GPU 提交时无需访问 asset/view/scene，
 * 直接 execute() 即可。
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

    // Geometry（来自 AssetGpuRegistry 的 GpuGeometry）
    Buffer* vertexBuffer = nullptr;
    Buffer* indexBuffer = nullptr;
    uint32_t indexCount = 0;
    IndexType indexType = IndexType::UInt32;  ///< 索引宽度（来自 Mesh，绑定 IB 与 drawIndexed 时用）
    uint32_t firstIndex = 0;
    int32_t baseVertex = 0;
    uint32_t vertexCount = 0;    // non-indexed draw 用
    uint32_t instanceCount = 1;  // 0 = 不绘制
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    uint32_t materialIndex = 0;

    // 纹理（binding 3~7, binding 8=sampler）。
    // 由 RenderCompiler 解析材质填充。空纹理由 execute 用 default* 退化。
    Texture* albedoTex = nullptr;    // binding 3
    Texture* normalTex = nullptr;    // binding 4
    Texture* mrTex = nullptr;        // binding 5 (metallicRoughness)
    Texture* emissiveTex = nullptr;  // binding 6
    Texture* aoTex = nullptr;        // binding 7
    Sampler* sampler = nullptr;      // binding 8

    // 对象数据（提交绘制时写入瞬态 Uniform 切片）
    math::Mat4 worldTransform{ 1.0f };

    // Sort / Meta
    uint64_t sortKey = 0;
    uint32_t pickId = 0;
    bool selected = false;
    bool hovered = false;
    bool visible = true;
    bool isWire = false;
    bool translucent = false;

    /// 提交到 CommandList。
    /// defaultWhite / defaultSampler：仅对声明了纹理 binding 的 PSO 有效；
    /// 若 albedoTex/sampler 为空，则用这俩默认值退化（保证无材质模型视觉不变）。
    ///
    /// frameBg：保存纹理与采样器等静态资源；Uniform 数据由显式切片绑定。
    void execute(CommandList& cmd, BindGroup& frameBg, const UniformSlice& sceneUniform,
                 const UniformSlice& materialUniform, Texture* defaultWhite = nullptr, Texture* defaultNormal = nullptr,
                 Texture* defaultMetallicRoughness = nullptr, Texture* defaultBlack = nullptr,
                 Sampler* defaultSampler = nullptr) const;
};

}  // namespace mulan::engine
