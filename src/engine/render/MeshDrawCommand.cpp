/**
 * @file MeshDrawCommand.cpp
 * @brief MeshDrawCommand::execute() 实现
 * @author hxxcxx
 * @date 2026-06-01
 */

#include "MeshDrawCommand.h"
#include "../rhi/BindGroup.h"

namespace mulan::engine {

void MeshDrawCommand::execute(CommandList& cmd,
                               Buffer* sceneUBO,
                               Buffer* objectUBO,
                               Buffer* materialUBO) const {
    if (instanceCount == 0 || !pipelineState || !vertexBuffer) return;

    cmd.setPipelineState(pipelineState);

    // Bind UBOs via BindGroup
    BindGroup bg;
    if (sceneUBO)    bg.addUBO(0, sceneUBO,    0, 256);
    if (objectUBO)   bg.addUBO(1, objectUBO,   objectUboOffset, 128);
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
