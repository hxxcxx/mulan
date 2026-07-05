/**
 * @file face_stage.h
 * @brief FaceStage 执行实体表面 bucket 的几何绘制。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_stage.h"
#include "../draw/geometry_draw_executor.h"

#include <span>

namespace mulan::engine {

class FaceStage final : public RenderStage {
public:
    FaceStage(RHIDevice& device, RenderResourceCache& gpu, MaterialCache& matCache, const LightEnvironment& lightEnv);

    std::string_view name() const override { return "Face"; }

    core::Result<void> init(RHIDevice& device, const RenderTargetInfo& target) override;

    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

    void setDrawCommands(std::span<const MeshDrawCommand> commands);
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT);

    PipelineState* pipelineState() const;
    Texture* defaultWhiteTexture() const;
    Sampler* defaultSampler() const;

private:
    GeometryDrawExecutor draw_executor_;
};

}  // namespace mulan::engine
