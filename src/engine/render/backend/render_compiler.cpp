#include "render_compiler.h"

#include "../asset_gpu_registry.h"
#include "../material/material_cache.h"
#include "../render_geometry.h"
#include "../texture_cache.h"
#include "../texture_loader.h"

#include <string>

namespace mulan::engine {
namespace {

uint32_t materialOffset(const RenderWorldSnapshot& snapshot, RenderMaterialHandle handle, MaterialCache& cache) {
    const auto* record = snapshot.material(handle);
    if (!record) {
        return cache.materialGpuOffset(0);
    }

    const std::string name =
            "render-material:" + std::to_string(handle.generation) + ":" + std::to_string(handle.index);
    const auto materialHandle = cache.registerMaterial(name, record->desc.material);
    return cache.materialGpuOffset(materialHandle);
}

Texture* loadTexture(TextureCache& cache, const RenderTextureDesc& desc) {
    if (desc.embeddedData.empty() && desc.sourcePath.empty())
        return nullptr;

    TextureLoadOptions options;
    options.sRGB = desc.srgb;

    // 内嵌字节优先（GLB bufferView / data: URI / 已加载内存字节）
    if (!desc.embeddedData.empty()) {
        auto* loaded =
                cache.loadFromMemory(desc.sourcePath, desc.embeddedData.data(), desc.embeddedData.size(), options);
        return loaded ? loaded->get() : nullptr;
    }
    auto* loaded = cache.load(desc.sourcePath, options);
    return loaded ? loaded->get() : nullptr;
}

void populateSurfaceTextures(const RenderWorldSnapshot& snapshot, const RenderWorkItem& item, TextureCache& cache,
                             MeshDrawCommand& command) {
    const auto* material = snapshot.material(item.material);
    if (!material)
        return;

    command.albedoTex = loadTexture(cache, material->desc.baseColorTexture);
    command.normalTex = loadTexture(cache, material->desc.normalTexture);
    command.mrTex = loadTexture(cache, material->desc.metallicRoughnessTexture);
    command.emissiveTex = loadTexture(cache, material->desc.emissiveTexture);
    command.aoTex = loadTexture(cache, material->desc.ambientOcclusionTexture);
}

MeshDrawCommand makeCommand(const RenderWorldSnapshot& snapshot, const RenderWorkItem& item,
                            const GpuGeometry& geometry, PipelineState* pipeline, uint32_t objectOffset,
                            uint32_t materialOffset, bool isEdge) {
    const auto* geometryRecord = snapshot.geometry(item.geometry);

    MeshDrawCommand command;
    command.pipelineState = pipeline;
    command.vertexBuffer = geometry.vertexBuffer.get();
    command.indexBuffer = geometry.indexBuffer.get();
    command.indexCount = geometry.indexCount;
    command.indexType = geometry.indexType;
    command.vertexCount = geometry.vertexCount;
    command.instanceCount = 1;
    command.topology = geometryRecord ? geometryRecord->desc.topology : PrimitiveTopology::TriangleList;
    command.objectUboOffset = objectOffset;
    command.materialUboOffset = materialOffset;
    command.worldTransform = item.worldTransform;
    command.pickId = item.pickId;
    command.selected = item.selected;
    command.isWire = isEdge;
    return command;
}

}  // namespace

void RenderCompiler::compile(const RenderWorldSnapshot& snapshot, const RenderWorkload& workload,
                             RenderCompileContext& context) {
    clear();

    uint32_t nextObjectOffset = 0;

    for (const auto& item : workload.surfaces()) {
        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord || geometryRecord->desc.empty || !geometryRecord->desc.mesh)
            continue;

        const auto* gpuGeometry =
                context.geometry.acquireGeometry(geometryRecord->desc.resourceKey, *geometryRecord->desc.mesh);
        if (!gpuGeometry)
            continue;

        auto command = makeCommand(snapshot, item, *gpuGeometry, context.surfacePipeline, nextObjectOffset,
                                   materialOffset(snapshot, item.material, context.materials), false);
        populateSurfaceTextures(snapshot, item, context.textures, command);
        surface_commands_.push_back(std::move(command));
        nextObjectOffset += MeshDrawCommand::kObjectUboStride;
    }

    for (const auto& item : workload.edges()) {
        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord || geometryRecord->desc.empty || !geometryRecord->desc.mesh)
            continue;

        const auto* gpuGeometry =
                context.geometry.acquireGeometry(geometryRecord->desc.resourceKey, *geometryRecord->desc.mesh);
        if (!gpuGeometry)
            continue;

        edge_commands_.push_back(makeCommand(snapshot, item, *gpuGeometry, context.edgePipeline, nextObjectOffset,
                                             context.materials.materialGpuOffset(0), true));
        nextObjectOffset += MeshDrawCommand::kObjectUboStride;
    }
}

void RenderCompiler::clear() {
    surface_commands_.clear();
    edge_commands_.clear();
}

}  // namespace mulan::engine
