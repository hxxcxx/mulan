#include "mesh_draw_command.h"
#include "../rhi/bind_group.h"
#include "../rhi/command_list.h"
#include "../rhi/buffer.h"
#include "gpu_scene_contract.h"
#include <mulan/math/math.h>

namespace mulan::engine {

void MeshDrawCommand::execute(CommandList& cmd, BindGroup& frameBg, Buffer* sceneUBO, Buffer* objectUBO,
                              Buffer* materialUBO, Texture* defaultWhite, Texture* defaultNormal,
                              Texture* defaultMetallicRoughness, Texture* defaultBlack, Sampler* defaultSampler) const {
    if (instanceCount == 0 || !pipelineState || !vertexBuffer)
        return;

    // 上传 per-object UBO
    if (objectUBO) {
        const ObjectUniforms obj = makeObjectUniforms(worldTransform, pickId, selected);
        objectUBO->update(objectUboOffset, sizeof(ObjectUniforms), &obj);
    }

    cmd.setPipelineState(pipelineState);

    // 刷新每 draw 变化的 binding：
    //   binding=1 (object UBO offset) / binding=2 (material UBO offset) 每 draw 必变；
    //   binding=3..8 纹理/sampler 可能变（按 draw 材质决定）。
    // 其余 binding（scene=0, IBL=10/11/12）帧内不变，复用 frameBg 缓存 descriptor。
    // 后端据 dirtyMask 走局部重写，未变化的 binding 零分配零写入。
    frameBg.updateUBO(1, objectUBO, objectUboOffset, kObjectUboStride);
    frameBg.updateUBO(2, materialUBO, materialUboOffset, 128);

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

    cmd.bindGroup(frameBg);

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
