#include "geometry_draw_executor.h"
#include "../device_pipeline_library.h"
#include "../../rhi/bind_group.h"
#include "../../rhi/render_types.h"
#include "../../rhi/render_state.h"
#include "../gpu_scene_contract.h"

#include <mulan/core/log/log.h>

#include <string>

namespace mulan::engine {

// ─── 构造 / init ───────────────────────────────────────────────

GeometryDrawExecutor::GeometryDrawExecutor(RHIDevice& device, GeometryDrawSharedResources& sharedResources,
                                           DrawFallbackResources& fallbackResources,
                                           DevicePipelineLibrary& pipelineLibrary, RenderTechnique technique)
    : device_(device),
      shared_resources_(sharedResources),
      fallback_resources_(fallbackResources),
      pipeline_library_(pipelineLibrary),
      technique_(TechniqueRegistry::builtin(technique)) {
}

bool GeometryDrawExecutor::init(TextureFormat colorFmt, TextureFormat depthFmt, bool hasDepth, uint32_t sampleCount) {
    pso_ = pipeline_library_.acquire(DevicePipelineKey{
            .technique = technique_.technique,
            .colorFormat = colorFmt,
            .depthFormat = depthFmt,
            .sampleCount = sampleCount,
            .hasDepth = hasDepth,
    });
    if (!pso_)
        return false;

    instanced_pso_ = nullptr;
    if (technique_.instancedVertexShader && technique_.instancedVertexShader[0] != '\0' &&
        device_.capabilities().maxUniformBufferBindingSize >= sizeof(ObjectBatchUniforms)) {
        instanced_pso_ = pipeline_library_.acquire(DevicePipelineKey{
                .technique = technique_.technique,
                .colorFormat = colorFmt,
                .depthFormat = depthFmt,
                .sampleCount = sampleCount,
                .hasDepth = hasDepth,
                .objectBindingMode = ObjectBindingMode::InstancedBatch,
        });
        // BindGroup 复用以完整 layout 等价为前提；不满足时只关闭优化，不能影响基础绘制。
        if (instanced_pso_ && instanced_pso_->bindGroupLayout() != pso_->bindGroupLayout()) {
            instanced_pso_ = nullptr;
        }
    }

    if (!createFrameBindGroup(colorFmt, depthFmt, hasDepth))
        return false;

    initialized_ = true;
    return true;
}

// ─── Execute ───────────────────────────────────────────────────

bool GeometryDrawExecutor::createFrameBindGroup(TextureFormat, TextureFormat, bool) {
    // Uniform binding 由每次 draw 提供切片；这里只保存纹理和采样器。
    BindGroupDesc bg;

    if (technique_.sampleTextures && fallback_resources_.whiteTexture() && fallback_resources_.sampler()) {
        bg.addTexture(3, fallback_resources_.whiteTexture());
        bg.addTexture(4, fallback_resources_.normalTexture() ? fallback_resources_.normalTexture()
                                                             : fallback_resources_.whiteTexture());
        bg.addTexture(5, fallback_resources_.metallicRoughnessTexture() ? fallback_resources_.metallicRoughnessTexture()
                                                                        : fallback_resources_.whiteTexture());
        bg.addTexture(6, fallback_resources_.blackTexture() ? fallback_resources_.blackTexture()
                                                            : fallback_resources_.whiteTexture());
        bg.addTexture(7, fallback_resources_.whiteTexture());
        bg.addSampler(8, fallback_resources_.sampler());
        // IBL 三件套：先用内置默认环境光照，每帧 execute 时刷新为真实烘焙产物。
        // 若 fallback 创建失败，退化到 defaultWhite 以保证 descriptor 非 null
        // —— 避免 Vulkan 验证层 "descriptor never updated" 错误。
        Texture* iblFallback = fallback_resources_.environmentTexture() ? fallback_resources_.environmentTexture()
                                                                        : fallback_resources_.whiteTexture();
        Texture* lutFallback = fallback_resources_.brdfLutTexture() ? fallback_resources_.brdfLutTexture()
                                                                    : fallback_resources_.whiteTexture();
        bg.addTexture(9, iblFallback);
        bg.addTexture(10, iblFallback);
        bg.addTexture(11, lutFallback);
    }

    auto result = device_.createBindGroup(pso_->bindGroupLayout(), bg);
    if (!result) {
        LOG_ERROR("[GeometryDrawExecutor] Bind-group creation failed: {}", result.error().message);
        return false;
    }
    frame_bg_ = std::move(*result);
    return true;
}

void GeometryDrawExecutor::execute(const DrawExecutionContext& ctx) {
    execution_stats_ = {};
    if (!initialized_ || !pso_ || !ctx.cmd || !frame_bg_)
        return;

    if (!shared_resources_.sceneUniform())
        return;

    // binding=9/10/11 (IBL 三件套) 在 setIBLTextures 后生效，每帧刷新一次。
    if (technique_.sampleTextures) {
        frame_bg_->updateTexture(9, ibl_irradiance_ ? ibl_irradiance_ : fallback_resources_.environmentTexture());
        frame_bg_->updateTexture(10, ibl_prefilter_ ? ibl_prefilter_ : fallback_resources_.environmentTexture());
        frame_bg_->updateTexture(11, ibl_brdf_lut_ ? ibl_brdf_lut_ : fallback_resources_.brdfLutTexture());
    }

    planGeometryDrawBatches(commands_, pso_, instanced_pso_ != nullptr, batch_plan_);
    for (const GeometryDrawBatchRange& range : batch_plan_) {
        if (!range.instanced || range.count > kObjectBatchCapacity) {
            for (size_t offset = 0; offset < range.count; ++offset) {
                const MeshDrawCommand& command = commands_[range.first + offset];
                if (!command.visible || command.instanceCount == 0)
                    continue;
                const auto materialUniform = shared_resources_.materialUniform(*ctx.cmd, command.materialIndex);
                if (!materialUniform)
                    continue;
                command.execute(*ctx.cmd, *frame_bg_, shared_resources_.sceneUniform(), *materialUniform,
                                fallback_resources_.whiteTexture(), fallback_resources_.normalTexture(),
                                fallback_resources_.metallicRoughnessTexture(), fallback_resources_.blackTexture(),
                                fallback_resources_.sampler());
                ++execution_stats_.legacyDrawCount;
            }
            continue;
        }

        const MeshDrawCommand& first = commands_[range.first];
        const auto materialUniform = shared_resources_.materialUniform(*ctx.cmd, first.materialIndex);
        if (!materialUniform)
            continue;

        // 固定写满 16 KiB：未使用项清零，descriptor range 在所有批次与后端上保持稳定。
        ObjectBatchUniforms objects{};
        if (!packGeometryDrawObjectBatch(commands_.subspan(range.first, range.count), objects))
            continue;
        const auto objectUniform = ctx.cmd->writeUniform(objects);
        if (!objectUniform)
            return;

        first.executePrepared(*ctx.cmd, *frame_bg_, shared_resources_.sceneUniform(), *objectUniform, *materialUniform,
                              instanced_pso_, static_cast<uint32_t>(range.count), fallback_resources_.whiteTexture(),
                              fallback_resources_.normalTexture(), fallback_resources_.metallicRoughnessTexture(),
                              fallback_resources_.blackTexture(), fallback_resources_.sampler());
        ++execution_stats_.instancedDrawCount;
        execution_stats_.batchedInstanceCount += range.count;
    }
}

}  // namespace mulan::engine
