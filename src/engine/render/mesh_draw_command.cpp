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

    cmd.setPipelineState(pipelineState);

    // 纹理 + sampler：仅当 defaultWhite 非 null（即该 pass 的 PSO 声明了纹理 binding）时才绑定。
    // 空纹理由 default* 退化：albedo→白, normal→(0,0,1)平面, mr→(1,1,0), emissive→黑, ao→白。
    if (defaultWhite) {
        frameBg.updateTexture(3, albedoTex ? albedoTex : defaultWhite);
        frameBg.updateTexture(4, normalTex ? normalTex : (defaultNormal ? defaultNormal : defaultWhite));
        frameBg.updateTexture(5, mrTex ? mrTex : (defaultMetallicRoughness ? defaultMetallicRoughness : defaultWhite));
        frameBg.updateTexture(6, emissiveTex ? emissiveTex : (defaultBlack ? defaultBlack : defaultWhite));
        frameBg.updateTexture(7, aoTex ? aoTex : defaultWhite);
        frameBg.updateSampler(8, sampler ? sampler : defaultSampler);
    }

    const std::array uniforms{ DynamicUniformBinding{ 0, sceneUniform }, DynamicUniformBinding{ 1, *objectUniform },
                               DynamicUniformBinding{ 2, materialUniform } };
    cmd.bindGroup(frameBg, uniforms);

    cmd.setVertexBuffer(0, vertexBuffer);

    if (indexBuffer && indexCount > 0) {
        cmd.setIndexBuffer(indexBuffer, 0, indexType);
        DrawIndexedAttribs attrs;
        attrs.indexCount = indexCount;
        attrs.instanceCount = instanceCount;
        attrs.startIndex = firstIndex;
        attrs.baseVertex = baseVertex;
        attrs.indexType = indexType;
        cmd.drawIndexed(attrs);
    } else if (vertexCount > 0) {
        DrawAttribs attrs;
        attrs.vertexCount = vertexCount;
        attrs.instanceCount = instanceCount;
        cmd.draw(attrs);
    }
}

}  // namespace mulan::engine
