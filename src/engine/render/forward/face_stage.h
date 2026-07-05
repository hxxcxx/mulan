#pragma once

#include "render_stage.h"
#include "../graph/geometry_pass.h"

#include <span>

namespace mulan::engine {

class FaceStage final : public RenderStage {
public:
    FaceStage(RHIDevice& device, RenderResourceCache& gpu,
              MaterialCache& matCache, const LightEnvironment& lightEnv);

    std::string_view name() const override { return "Face"; }

    std::expected<void, core::Error>
    init(RHIDevice& device, const RenderTargetInfo& target) override;

    void shutdown(RHIDevice& device) override;
    void execute(RenderFrame& frame) override;

    void setDrawCommands(std::span<const MeshDrawCommand> commands);
    void setIBLTextures(Texture* irradiance, Texture* prefilter, Texture* brdfLUT);

    PipelineState* pipelineState() const;
    Texture* defaultWhiteTexture() const;
    Sampler* defaultSampler() const;

private:
    GeometryPass pass_;
};

} // namespace mulan::engine
