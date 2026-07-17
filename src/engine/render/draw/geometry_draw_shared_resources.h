/**
 * @file geometry_draw_shared_resources.h
 * @brief 管理几何绘制所需的场景与材质 Uniform 状态。
 * @author hxxcxx
 * @date 2026-07-06
 */

#pragma once

#include "../draw/draw_execution_context.h"
#include "../gpu_scene_contract.h"
#include "../../rhi/buffer.h"
#include "../../rhi/uniform_slice.h"

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

    void uploadFrameData(const DrawExecutionContext& ctx, const LightEnvironment& lightEnv);

    const UniformSlice& sceneUniform() const { return scene_uniform_; }
    std::optional<UniformSlice> materialUniform(CommandList& commandList, uint32_t materialIndex);

private:
    SceneUniforms buildSceneUniforms(const DrawExecutionContext& ctx, const LightEnvironment& lightEnv) const;

    RHIDevice& device_;
    MaterialCache& material_cache_;
    UniformSlice scene_uniform_;
    std::unordered_map<uint32_t, UniformSlice> material_uniforms_;
};

}  // namespace mulan::engine
