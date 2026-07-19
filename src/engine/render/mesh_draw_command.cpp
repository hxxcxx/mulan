#include "mesh_draw_command.h"
#include "../rhi/bind_group.h"
#include "../rhi/command_list.h"
#include "../rhi/buffer.h"
#include "gpu_scene_contract.h"
#include <mulan/math/math.h>

#include <array>

namespace mulan::engine {

void MeshDrawCommand::execute(CommandList& cmd, BindGroup& frameBg, const UniformSlice& sceneUniform,
                              const UniformSlice& materialUniform, Texture* defaultWhite, Texture* defaultNormal,
                              Texture* defaultMetallicRoughness, Texture* defaultBlack, Sampler* defaultSampler) const {
    if (instanceCount == 0 || !pipelineState || !vertexBuffer)
        return;

    const ObjectUniforms obj = makeObjectUniforms(worldTransform, pickId, selected, hovered);
    const auto objectUniform = cmd.writeUniform(obj);
    if (!objectUniform)
        return;

    executePrepared(cmd, frameBg, sceneUniform, *objectUniform, materialUniform, pipelineState, instanceCount,
                    defaultWhite, defaultNormal, defaultMetallicRoughness, defaultBlack, defaultSampler);
}

void MeshDrawCommand::executePrepared(CommandList& cmd, BindGroup& frameBg, const UniformSlice& sceneUniform,
                                      const UniformSlice& objectUniform, const UniformSlice& materialUniform,
                                      PipelineState* activePipeline, uint32_t activeInstanceCount,
                                      Texture* defaultWhite, Texture* defaultNormal, Texture* defaultMetallicRoughness,
                                      Texture* defaultBlack, Sampler* defaultSampler) const {
    if (activeInstanceCount == 0 || !activePipeline || !vertexBuffer || !objectUniform)
        return;

    cmd.setPipelineState(activePipeline);

    // 材质 profile 与 PSO layout 一致；只更新该 family 声明的 binding。
    if (materialBindings != MaterialBindingProfile::None && defaultWhite) {
        frameBg.updateTexture(3, albedoTex ? albedoTex : defaultWhite);
        frameBg.updateSampler(8, sampler ? sampler : defaultSampler);
        frameBg.updateTexture(13, opacityTex ? opacityTex : defaultWhite);
    }
    if ((materialBindings == MaterialBindingProfile::Legacy || materialBindings == MaterialBindingProfile::PBR) &&
        defaultWhite) {
        frameBg.updateTexture(4, normalTex ? normalTex : (defaultNormal ? defaultNormal : defaultWhite));
        frameBg.updateTexture(6, emissiveTex ? emissiveTex : (defaultBlack ? defaultBlack : defaultWhite));
    }
    if (materialBindings == MaterialBindingProfile::Legacy && defaultWhite) {
        frameBg.updateTexture(5, specularTex ? specularTex : defaultWhite);
        frameBg.updateTexture(7, shininessTex ? shininessTex : defaultWhite);
        frameBg.updateTexture(12, ambientTex ? ambientTex : defaultWhite);
    }
    if (materialBindings == MaterialBindingProfile::PBR && defaultWhite) {
        frameBg.updateTexture(5, mrTex ? mrTex : (defaultMetallicRoughness ? defaultMetallicRoughness : defaultWhite));
        frameBg.updateTexture(7, aoTex ? aoTex : defaultWhite);
    }

    const std::array uniforms{ DynamicUniformBinding{ 0, sceneUniform }, DynamicUniformBinding{ 1, objectUniform },
                               DynamicUniformBinding{ 2, materialUniform } };
    cmd.bindGroup(frameBg, uniforms);

    cmd.setVertexBuffer(0, vertexBuffer);

    if (indexBuffer && indexCount > 0) {
        cmd.setIndexBuffer(indexBuffer, 0, indexType);
        DrawIndexedAttribs attrs;
        attrs.indexCount = indexCount;
        attrs.instanceCount = activeInstanceCount;
        attrs.startIndex = firstIndex;
        attrs.baseVertex = baseVertex;
        attrs.indexType = indexType;
        attrs.startInstance = 0;
        cmd.drawIndexed(attrs);
    } else if (vertexCount > 0) {
        DrawAttribs attrs;
        attrs.vertexCount = vertexCount;
        attrs.instanceCount = activeInstanceCount;
        attrs.startInstance = 0;
        cmd.draw(attrs);
    }
}

}  // namespace mulan::engine
