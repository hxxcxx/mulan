/**
 * @file geometry_pass.h
 * @brief GeometryPass —— 通用几何绘制通道
 * @author hxxcxx
 * @date 2026-07-03
 *
 * 消费 MeshDrawCommand 并逐条绘制。它的可变部分（shader / 拓扑 / 是否写深度）
 * 由 GeometryPassConfig 在构造时指定；绘制逻辑（上传 Scene/Object/Material
 * UBO、遍历命令、执行 MeshDrawCommand）对所有配置完全相同。
 *
 * 因此同一个 GeometryPass 类可表达多种“画法”：实体面、边线、线框、拾取、
 * 选中高亮等，只需不同的配置，无需复制 pass 代码。
 */

#pragma once

#include "render_pass.h"
#include "../render_resource_cache.h"
#include "../mesh_draw_command.h"
#include "../light_environment.h"
#include "../../rhi/device.h"
#include "../../rhi/buffer.h"
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

/// GeometryPass 的可变配置。值类型，构造时传入后不再变更。
struct GeometryPassConfig {
    /// 着色器基名（不含扩展名），按 `<base>.vert` / `<base>.frag` 加载。
    /// 例如 "solid" → solid.vert / solid.frag。
    const char* shaderBase = "solid";
    /// 图元拓扑（TriangleList / LineList / ...）
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    /// 是否写深度缓冲
    bool depthWrite = true;
    /// Pass 名（调试 / 日志用）
    const char* passName = "Geometry";
    /// 是否声明纹理/sampler binding 并采样 albedo 纹理。
    /// true  → PSO 声明 binding=3(TextureSRV)/4(Sampler)，draw 命令绑定纹理
    /// false → 仅 3 个 UBO，纯色快速路径（edge/pick/wireframe）
    bool sampleTextures = false;
};

class GeometryPass : public RenderPass {
public:
    GeometryPass(RHIDevice& device, RenderResourceCache& gpu,
                 MaterialCache& matCache, const LightEnvironment& lightEnv,
                 GeometryPassConfig cfg);

    const char* name() const override { return cfg_.passName; }

    bool init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    void execute(const PassContext& ctx) override;

    void setDrawCommands(std::span<const MeshDrawCommand> cmds) { commands_ = cmds; }

    PipelineState* pipelineState() const { return pso_.get(); }

    /// 全局默认白纹理（无材质模型退化用，1×1 RGBA=(255,255,255,255)）。
    /// 仅 sampleTextures=true 时非 null。
    Texture* defaultWhiteTexture() const { return default_white_tex_; }
    /// 全局默认线性 sampler。仅 sampleTextures=true 时非 null。
    Sampler* defaultSampler() const { return default_sampler_.get(); }

private:
    bool loadShaders();
    bool createPSO(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth);
    bool createDefaultResources();
    void uploadSceneUBO(const PassContext& ctx);

    RHIDevice&              device_;
    RenderResourceCache&    gpu_;
    MaterialCache&          mat_cache_;
    const LightEnvironment& light_env_;
    GeometryPassConfig      cfg_;

    std::unique_ptr<Shader>        vs_;
    std::unique_ptr<Shader>        fs_;
    std::unique_ptr<PipelineState> pso_;
    std::unique_ptr<Buffer>        scene_ubo_;    // set=0, binding=0
    std::unique_ptr<Buffer>        object_ubo_;   // set=0, binding=1
    std::unique_ptr<Buffer>        material_ubo_; // set=0, binding=2

    // 仅 sampleTextures=true 时持有：默认 sampler + 1×1 白纹理
    std::unique_ptr<Sampler>       default_sampler_;
    Texture*                       default_white_tex_ = nullptr;  // 借用 TextureCache 中的资产

    std::span<const MeshDrawCommand> commands_;
    bool initialized_ = false;
};

} // namespace mulan::engine
