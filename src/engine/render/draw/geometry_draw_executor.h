/**
 * @file geometry_draw_executor.h
 * @brief GeometryDrawExecutor —— 通用几何绘制执行器
 * @author hxxcxx
 * @date 2026-07-03
 *
 * 消费 MeshDrawCommand 并逐条绘制。它的可变部分（shader / 拓扑 / 是否写深度）
 * 由 TechniqueDesc 在构造时指定；绘制逻辑（上传 Scene/Object/Material
 * UBO、遍历命令、执行 MeshDrawCommand）对所有配置完全相同。
 *
 * 因此同一个 GeometryDrawExecutor 类可表达多种“画法”：实体面、边线、线框、
 * 选中高亮等，只需不同的配置，无需复制 pass 代码。
 */

#pragma once

#include "draw_execution_context.h"
#include "geometry_draw_shared_resources.h"
#include "../mesh_draw_command.h"
#include "../technique/render_technique.h"
#include "../../rhi/device.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/render_types.h"
#include <mulan/math/math.h>

#include <cstdint>
#include <span>
#include <string>

namespace mulan::engine {

class DevicePipelineLibrary;

class GeometryDrawExecutor : public DrawExecutor {
public:
    GeometryDrawExecutor(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                         DevicePipelineLibrary& pipelineLibrary, RenderTechnique technique);

    const char* name() const override { return technique_.debugName; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth, uint32_t sampleCount);
    void execute(const DrawExecutionContext& ctx) override;

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { commands_ = cmds; }

    PipelineState* pipelineState() const { return pso_; }

    /// 全局默认白纹理（无材质模型退化用，1×1 RGBA=(255,255,255,255)）。
    /// 仅 sampleTextures=true 时非 null。
    Texture* defaultWhiteTexture() const { return shared_resources_.defaultWhiteTexture(); }
    /// 全局默认线性 sampler。仅 sampleTextures=true 时非 null。
    Sampler* defaultSampler() const { return shared_resources_.defaultSampler(); }

    /// 设置 IBL 三件套（irradiance / prefilter / brdf LUT，均为 equirect 2D）。
    /// 任意一张为 nullptr 时该 binding 走 default fallback。
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
        ibl_irradiance_ = irradiance;
        ibl_prefilter_ = prefilter;
        ibl_brdf_lut_ = brdfLUT;
    }

private:
    bool createFrameBindGroup(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);

    RHIDevice& device_;
    GeometryDrawSharedResources& shared_resources_;
    DevicePipelineLibrary& pipeline_library_;
    const TechniqueDesc& technique_;

    PipelineState* pso_ = nullptr;

    /// 保存纹理与采样器等静态资源；Uniform 在提交 draw 时通过切片绑定。
    std::unique_ptr<BindGroup> frame_bg_;

    // 借用自 Renderer::IBLPipeline 的烘焙产物。
    Texture* ibl_irradiance_ = nullptr;
    Texture* ibl_prefilter_ = nullptr;
    Texture* ibl_brdf_lut_ = nullptr;

    std::span<const MeshDrawCommand> commands_;
    bool initialized_ = false;
};

}  // namespace mulan::engine
