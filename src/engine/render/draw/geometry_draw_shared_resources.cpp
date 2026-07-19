#include "geometry_draw_shared_resources.h"

#include "../light_environment.h"
#include "../material/material_cache.h"
#include "../mesh_draw_command.h"
#include "../../rhi/device.h"

#include <mulan/core/log/log.h>

namespace mulan::engine {
GeometryDrawSharedResources::GeometryDrawSharedResources(RHIDevice& device, MaterialCache& materialCache)
    : device_(device), material_cache_(materialCache) {
}

void GeometryDrawSharedResources::uploadFrameData(const DrawExecutionContext& ctx, const LightEnvironment& lightEnv) {
    material_uniforms_.clear();
    scene_uniform_ = {};
    const auto result = ctx.cmd->writeUniform(buildSceneUniforms(ctx, lightEnv));
    if (!result) {
        LOG_ERROR("[GeometryDrawSharedResources] Scene uniform allocation failed: {}", result.error().message);
        return;
    }
    scene_uniform_ = *result;
}

std::optional<UniformSlice> GeometryDrawSharedResources::materialUniform(CommandList& commandList,
                                                                         uint32_t materialIndex) {
    if (const auto cached = material_uniforms_.find(materialIndex); cached != material_uniforms_.end())
        return cached->second;

    const Material* material = material_cache_.find(materialIndex);
    if (!material)
        material = material_cache_.find(0);
    if (!material)
        return std::nullopt;

    const MaterialGPU gpu = MaterialGPU::fromMaterial(*material);
    const auto result = commandList.writeUniform(gpu);
    if (!result)
        return std::nullopt;
    material_uniforms_.emplace(materialIndex, *result);
    return *result;
}

SceneUniforms GeometryDrawSharedResources::buildSceneUniforms(const DrawExecutionContext& ctx,
                                                              const LightEnvironment& lightEnv) const {
    math::Mat4 clip = device_.clipSpaceCorrectionMatrix();
    math::Mat4 view = ctx.camera.viewMatrix;
    math::Mat4 proj = ctx.camera.projectionMatrix;
    math::Mat4 vp = clip * proj * view;
    const math::Vec3 eye = ctx.camera.eyePosition.asVec();

    const ResolvedLighting lighting = resolveLighting(lightEnv, view);
    const Light legacyLight = lighting.lightCount != 0
                                      ? lighting.lights[0]
                                      : Light::directional(math::Vec3(0.0, 0.0, -1.0), math::Vec3(0.0));

    SceneUniforms ubo{};
    storeGpuMat4(ubo.view, view);
    storeGpuMat4(ubo.projection, proj);
    storeGpuMat4(ubo.viewProjection, vp);
    storeGpuVec3(ubo.cameraPos, eye);
    // 保留旧单灯字段供 ViewCube 和旧着色路径消费；表面 Shader 使用下方 lights 数组。
    storeGpuVec3(ubo.lightDir, legacyLight.direction);
    storeGpuVec3(ubo.lightColor, legacyLight.color * legacyLight.intensity);
    storeGpuVec3(ubo.ambientColor, lighting.ambientColor);
    storeGpuVec3(ubo.edgeColor, math::Vec3(0.08, 0.08, 0.08));
    storeGpuVec3(ubo.highlightColor, math::Vec3(1.0, 0.5, 0.0));
    for (uint32_t index = 0; index < lighting.lightCount; ++index)
        ubo.lights[index] = makeSceneLightUniform(lighting.lights[index]);
    ubo.lightCount = lighting.lightCount;
    ubo.exposure = toFiniteGpuFloat(lighting.exposure);

    return ubo;
}

}  // namespace mulan::engine
