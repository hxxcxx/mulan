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
#include "../mesh_draw_command.h"
#include "../light_environment.h"
#include "../technique/render_technique.h"
#include "../../rhi/device.h"
#include "../../rhi/buffer.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/pipeline_state.h"
#include "../../rhi/render_types.h"
#include "../../rhi/shader.h"
#include "../../rhi/sampler.h"
#include "../../rhi/texture.h"
#include <mulan/math/math.h>

#include <cstdint>
#include <span>
#include <string>

namespace mulan::engine {

class MaterialCache;

class GeometryDrawExecutor : public DrawExecutor {
public:
    GeometryDrawExecutor(RHIDevice& device, MaterialCache& matCache, const LightEnvironment& lightEnv,
                         RenderTechnique technique);

    const char* name() const override { return technique_.debugName; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const DrawExecutionContext& ctx) override;

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { commands_ = cmds; }

    PipelineState* pipelineState() const { return pso_.get(); }

    /// 全局默认白纹理（无材质模型退化用，1×1 RGBA=(255,255,255,255)）。
    /// 仅 sampleTextures=true 时非 null。
    Texture* defaultWhiteTexture() const { return default_white_tex_.get(); }
    /// 全局默认线性 sampler。仅 sampleTextures=true 时非 null。
    Sampler* defaultSampler() const { return default_sampler_.get(); }

    /// 设置 IBL 三件套（irradiance / prefilter / brdf LUT，均为 equirect 2D）。
    /// 任意一张为 nullptr 时该 binding 走 default fallback。
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT) {
        ibl_irradiance_ = irradiance;
        ibl_prefilter_ = prefilter;
        ibl_brdf_lut_ = brdfLUT;
    }

private:
    bool loadShaders();
    bool createPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    bool createDefaultResources();
    bool createFrameBindGroup(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void uploadSceneUBO(const DrawExecutionContext& ctx);

    RHIDevice& device_;
    MaterialCache& mat_cache_;
    const LightEnvironment& light_env_;
    const TechniqueDesc& technique_;

    std::unique_ptr<Shader> vs_;
    std::unique_ptr<Shader> fs_;
    std::unique_ptr<PipelineState> pso_;
    std::unique_ptr<Buffer> scene_ubo_;     // set=0, binding=0
    std::unique_ptr<Buffer> object_ubo_;    // set=0, binding=1
    std::unique_ptr<Buffer> material_ubo_;  // set=0, binding=2

    /// per-frame BindGroup（按 PSO layout 创建，binding=0/1/2 + 纹理槽）。
    /// 帧内 scene/material/texture binding 不变，仅每 draw 通过 updateUBO(1,...)
    /// 刷新 object UBO offset —— 后端走局部重写路径，descriptor set 复用。
    std::unique_ptr<BindGroup> frame_bg_;

    // 仅 sampleTextures=true 时持有：默认 sampler + 1×1 白纹理（本 pass 独占所有权）
    std::unique_ptr<Sampler> default_sampler_;
    std::unique_ptr<Texture> default_white_tex_;

    // IBL 默认 fallback：黑色 1×1 RGBA16F 2D + (1,0) RG16F LUT
    std::unique_ptr<Texture> default_ibl_tex_;
    std::unique_ptr<Texture> default_brdf_lut_;
    // 借用自 Renderer::IBLPipeline 的烘焙产物
    Texture* ibl_irradiance_ = nullptr;
    Texture* ibl_prefilter_ = nullptr;
    Texture* ibl_brdf_lut_ = nullptr;

    std::span<const MeshDrawCommand> commands_;
    bool initialized_ = false;
};

}  // namespace mulan::engine
