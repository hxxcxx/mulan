#include "mesh_draw_command.h"
#include "../rhi/bind_group.h"
#include "../rhi/command_list.h"
#include "../rhi/buffer.h"
#include <mulan/math/math.h>

namespace mulan::engine {

// ─── Object UBO 布局（与 Common.hlsli cbuffer Object 一致）───
#pragma pack(push, 1)
struct alignas(16) ObjectUniforms {
    float world[16];       // float4x4 World (column-major)
    float normalMat[12];   // float3x3 NormalMatrix (column-major, 3x4 for alignment)
    uint32_t pickId;       // uint PickId
    uint32_t selected;     // uint Selected
    float   _pad[2];       // 对齐到 128 bytes
};
#pragma pack(pop)
static_assert(sizeof(ObjectUniforms) == 128);

void MeshDrawCommand::execute(CommandList& cmd,
                               Buffer* sceneUBO,
                               Buffer* objectUBO,
                               Buffer* materialUBO,
                               Texture* defaultWhite,
                               Sampler* defaultSampler,
                               Texture* defaultEnvMap) const {
    if (instanceCount == 0 || !pipelineState || !vertexBuffer) return;

    // 上传 per-object UBO
    if (objectUBO) {
        ObjectUniforms obj{};
        // world matrix → float[16] (column-major)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                obj.world[c * 4 + r] = static_cast<float>(worldTransform[c][r]);

        // NormalMatrix = transpose(inverse(upper3x3(world)))。
        // 取 world 左上 3x3，求逆再转置，得到正确的法线变换矩阵。
        // 对于正交变换（纯旋转 + 均匀缩放）退化为 upper3x3 本身，
        // 与之前的简化实现一致；对非均匀缩放/剪切给出正确的法线。
        math::Mat3 upper(worldTransform);
        math::Mat3 normalMat = upper.inverse().transposed();
        // 列主序存储，与上方 World 同约定
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                obj.normalMat[c * 4 + r] = static_cast<float>(normalMat[c][r]);

        obj.pickId   = pickId;
        obj.selected = selected ? 1u : 0u;

        objectUBO->update(objectUboOffset, sizeof(ObjectUniforms), &obj);
    }

    cmd.setPipelineState(pipelineState);

    // Bind UBOs via BindGroupDesc
    BindGroupDesc bg;
    if (sceneUBO)    bg.addUBO(0, sceneUBO,    0, 288);
    if (objectUBO)   bg.addUBO(1, objectUBO,   objectUboOffset, kObjectUboStride);
    if (materialUBO) bg.addUBO(2, materialUBO, materialUboOffset, 128);

    // 纹理 + sampler：仅当 defaultWhite 非 null（即该 pass 的 PSO 声明了纹理 binding）时才绑定。
    // 空纹理退化到默认值：albedo→白, normal→(0,0,1)平面, mr→(1,1,0), emissive→黑, ao→白。
    if (defaultWhite) {
        bg.addTexture(3, albedoTex ? albedoTex : defaultWhite);
        bg.addTexture(4, normalTex ? normalTex : defaultWhite);
        bg.addTexture(5, mrTex     ? mrTex     : defaultWhite);
        bg.addTexture(6, emissiveTex ? emissiveTex : defaultWhite);
        bg.addTexture(7, aoTex     ? aoTex     : defaultWhite);
        bg.addSampler(8, sampler ? sampler : defaultSampler);
        bg.addTexture(9, envMap    ? envMap    : (defaultEnvMap ? defaultEnvMap : defaultWhite));
    }

    cmd.bindResources(bg);

    cmd.setVertexBuffer(0, vertexBuffer);

    if (indexBuffer && indexCount > 0) {
        cmd.setIndexBuffer(indexBuffer);
        DrawIndexedAttribs attrs;
        attrs.indexCount    = indexCount;
        attrs.instanceCount = instanceCount;
        attrs.startIndex    = firstIndex;
        attrs.baseVertex    = baseVertex;
        cmd.drawIndexed(attrs);
    } else if (vertexCount > 0) {
        DrawAttribs attrs;
        attrs.vertexCount   = vertexCount;
        attrs.instanceCount = instanceCount;
        cmd.draw(attrs);
    }
}

} // namespace mulan::engine
