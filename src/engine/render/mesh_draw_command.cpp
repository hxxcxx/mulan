#include "mesh_draw_command.h"
#include "../rhi/bind_group.h"
#include "../rhi/command_list.h"
#include "../rhi/buffer.h"

namespace mulan::engine {

// ─── Object UBO 布局（与 Common.hlsli cbuffer Object 一致）───
#pragma pack(push, 1)
struct alignas(16) ObjectUniforms {
    float world[16];       // float4x4 World
    float normalMat[12];   // float3x3 NormalMatrix (row-major, 3x4 for alignment)
    uint32_t pickId;       // uint PickId
    uint32_t selected;     // uint Selected
    float   _pad[2];       // 对齐到 128 bytes
};
#pragma pack(pop)
static_assert(sizeof(ObjectUniforms) == 128);

void MeshDrawCommand::execute(CommandList& cmd,
                               Buffer* sceneUBO,
                               Buffer* objectUBO,
                               Buffer* materialUBO) const {
    if (instanceCount == 0 || !pipelineState || !vertexBuffer) return;

    // 上传 per-object UBO
    if (objectUBO) {
        ObjectUniforms obj{};
        // world matrix → float[16] (column-major)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                obj.world[c * 4 + r] = static_cast<float>(worldTransform[c][r]);

        // NormalMatrix = transpose(inverse(upper3x3(world)))
        // 对于正交矩阵，NormalMatrix = upper3x3(world)
        // 使用 float3x3 → 存储为 3 行 float[4]（每行 padding 1 float）
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                obj.normalMat[r * 4 + c] = static_cast<float>(worldTransform[r][c]);

        obj.pickId   = pickId;
        obj.selected = selected ? 1u : 0u;

        objectUBO->update(objectUboOffset, sizeof(ObjectUniforms), &obj);
    }

    cmd.setPipelineState(pipelineState);

    // Bind UBOs via BindGroup
    BindGroup bg;
    if (sceneUBO)    bg.addUBO(0, sceneUBO,    0, 288);
    if (objectUBO)   bg.addUBO(1, objectUBO,   objectUboOffset, kObjectUboStride);
    if (materialUBO) bg.addUBO(2, materialUBO, materialUboOffset, 128);
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
