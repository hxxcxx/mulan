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
                               BindGroup& frameBg,
                               Buffer* sceneUBO,
                               Buffer* objectUBO,
                               Buffer* materialUBO,
                               Texture* defaultWhite,
                               Sampler* defaultSampler) const {
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
        frameBg.updateTexture(4, normalTex ? normalTex : defaultWhite);
        frameBg.updateTexture(5, mrTex     ? mrTex     : defaultWhite);
        frameBg.updateTexture(6, emissiveTex ? emissiveTex : defaultWhite);
        frameBg.updateTexture(7, aoTex     ? aoTex     : defaultWhite);
        frameBg.updateSampler(8, sampler ? sampler : defaultSampler);
    }

    cmd.bindGroup(frameBg);

    cmd.setVertexBuffer(0, vertexBuffer);

    if (indexBuffer && indexCount > 0) {
        cmd.setIndexBuffer(indexBuffer, 0, indexType);
        DrawIndexedAttribs attrs;
        attrs.indexCount    = indexCount;
        attrs.instanceCount = instanceCount;
        attrs.startIndex    = firstIndex;
        attrs.baseVertex    = baseVertex;
        attrs.indexType     = indexType;
        cmd.drawIndexed(attrs);
    } else if (vertexCount > 0) {
        DrawAttribs attrs;
        attrs.vertexCount   = vertexCount;
        attrs.instanceCount = instanceCount;
        cmd.draw(attrs);
    }
}

} // namespace mulan::engine
