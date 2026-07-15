/**
 * @file geometry_draw_shared_resources.h
 * @brief 用于管理几何绘制所需的共享资源，包括 UBO、默认纹理和采样器等。
 * @author hxxcxx
 * @date 2026-07-06
 */

#pragma once

#include "../draw/draw_execution_context.h"
#include "../gpu_scene_contract.h"
#include "../../rhi/buffer.h"
#include "../../rhi/sampler.h"
#include "../../rhi/texture.h"
#include "../../rhi/uniform_slice.h"

#include <memory>
#include <optional>
#include <unordered_map>

namespace mulan::engine {

class LightEnvironment;
class MaterialCache;
class RHIDevice;

class GeometryDrawSharedResources {
public:
    GeometryDrawSharedResources(RHIDevice& device, MaterialCache& materialCache);

    GeometryDrawSharedResources(const GeometryDrawSharedResources&) = delete;
    GeometryDrawSharedResources& operator=(const GeometryDrawSharedResources&) = delete;

    bool init();
    void uploadFrameData(const DrawExecutionContext& ctx, const LightEnvironment& lightEnv);

    const UniformSlice& sceneUniform() const { return scene_uniform_; }
    std::optional<UniformSlice> materialUniform(CommandList& commandList, uint32_t materialIndex);

    Texture* defaultWhiteTexture() const { return default_white_tex_.get(); }
    Texture* defaultBlackTexture() const { return default_black_tex_.get(); }
    Texture* defaultNormalTexture() const { return default_normal_tex_.get(); }
    Texture* defaultMetallicRoughnessTexture() const { return default_mr_tex_.get(); }
    Sampler* defaultSampler() const { return default_sampler_.get(); }
    Texture* defaultIBLTexture() const { return default_ibl_tex_.get(); }
    Texture* defaultBrdfLUT() const { return default_brdf_lut_.get(); }

private:
    bool createDefaultResources();
    SceneUniforms buildSceneUniforms(const DrawExecutionContext& ctx, const LightEnvironment& lightEnv) const;

    RHIDevice& device_;
    MaterialCache& material_cache_;
    UniformSlice scene_uniform_;
    std::unordered_map<uint32_t, UniformSlice> material_uniforms_;
    std::unique_ptr<Sampler> default_sampler_;
    std::unique_ptr<Texture> default_white_tex_;
    std::unique_ptr<Texture> default_black_tex_;
    std::unique_ptr<Texture> default_normal_tex_;
    std::unique_ptr<Texture> default_mr_tex_;
    std::unique_ptr<Texture> default_ibl_tex_;
    std::unique_ptr<Texture> default_brdf_lut_;
};

}  // namespace mulan::engine
