#include "render_compiler.h"

#include "../material/material_cache.h"
#include "../render_resource_cache.h"
#include "../texture_cache.h"

#include <string>

namespace mulan::engine {
namespace {

uint64_t geometryCacheKey(GeometryHandle handle) {
    return (static_cast<uint64_t>(handle.generation) << 32u) | handle.index;
}

uint32_t materialOffset(const RenderWorldSnapshot& snapshot,
                        RenderMaterialHandle handle,
                        MaterialCache& cache) {
    const auto* record = snapshot.material(handle);
    if (!record) {
        return cache.materialGpuOffset(0);
    }

    const std::string name = "render-material:" +
        std::to_string(handle.generation) + ":" + std::to_string(handle.index);
    const auto materialHandle = cache.registerMaterial(name, record->desc.material);
    return cache.materialGpuOffset(materialHandle);
}

Texture* loadTexture(TextureCache& cache, const RenderTextureDesc& desc) {
    if (desc.sourcePath.empty()) return nullptr;
    auto* loaded = cache.load(desc.sourcePath);
    return loaded ? loaded->get() : nullptr;
}

void populateSurfaceTextures(const RenderWorldSnapshot& snapshot,
                             const RenderWorkItem& item,
                             TextureCache& cache,
                             MeshDrawCommand& command) {
    const auto* material = snapshot.material(item.material);
    if (!material) return;

    command.albedoTex = loadTexture(cache, material->desc.baseColorTexture);
    command.normalTex = loadTexture(cache, material->desc.normalTexture);
    command.mrTex = loadTexture(cache, material->desc.metallicRoughnessTexture);
    command.emissiveTex = loadTexture(cache, material->desc.emissiveTexture);
    command.aoTex = loadTexture(cache, material->desc.ambientOcclusionTexture);
}

MeshDrawCommand makeCommand(const RenderWorldSnapshot& snapshot,
                            const RenderWorkItem& item,
                            const GpuGeometry& geometry,
                            PipelineState* pipeline,
                            uint32_t objectOffset,
                            uint32_t materialOffset,
                            bool isEdge) {
    const auto* geometryRecord = snapshot.geometry(item.geometry);

    MeshDrawCommand command;
    command.pipelineState = pipeline;
    command.vertexBuffer = geometry.vertexBuffer.get();
    command.indexBuffer = geometry.indexBuffer.get();
    command.indexCount = geometry.indexCount;
    command.indexType = geometry.indexType;
    command.vertexCount = geometry.vertexCount;
    command.instanceCount = 1;
    command.topology = geometryRecord ? geometryRecord->desc.mesh.topology
                                      : PrimitiveTopology::TriangleList;
    command.objectUboOffset = objectOffset;
    command.materialUboOffset = materialOffset;
    command.worldTransform = item.worldTransform;
    command.pickId = item.pickId;
    command.selected = item.selected;
    command.isWire = isEdge;
    return command;
}

} // namespace

void RenderCompiler::compile(const RenderWorldSnapshot& snapshot,
                             const RenderWorkload& workload,
                             RenderCompileContext& context) {
    clear();

    uint32_t nextObjectOffset = 0;

    for (const auto& item : workload.surfaces()) {
        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord || geometryRecord->desc.mesh.empty()) continue;

        const auto key = geometryCacheKey(item.geometry);
        const auto* gpuGeometry =
            context.resources.ensureSolidGeometry(key, geometryRecord->desc.mesh);
        if (!gpuGeometry) continue;

        auto command = makeCommand(snapshot, item, *gpuGeometry,
                                   context.surfacePipeline,
                                   nextObjectOffset,
                                   materialOffset(snapshot, item.material, context.materials),
                                   false);
        populateSurfaceTextures(snapshot, item, context.textures, command);
        surface_commands_.push_back(std::move(command));
        nextObjectOffset += MeshDrawCommand::kObjectUboStride;
    }

    for (const auto& item : workload.edges()) {
        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord || geometryRecord->desc.mesh.empty()) continue;

        const auto key = geometryCacheKey(item.geometry);
        const auto* gpuGeometry =
            context.resources.ensureWireGeometry(key, geometryRecord->desc.mesh);
        if (!gpuGeometry) continue;

        edge_commands_.push_back(makeCommand(snapshot, item, *gpuGeometry,
                                             context.edgePipeline,
                                             nextObjectOffset,
                                             context.materials.materialGpuOffset(0),
                                             true));
        nextObjectOffset += MeshDrawCommand::kObjectUboStride;
    }
}

void RenderCompiler::clear() {
    surface_commands_.clear();
    edge_commands_.clear();
}

} // namespace mulan::engine
