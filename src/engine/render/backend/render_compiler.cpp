#include "render_compiler.h"

#include "../asset_gpu_registry.h"
#include "../frontend/render_contract.h"
#include "../material/material_cache.h"
#include "../render_geometry.h"

#include <string>

namespace mulan::engine {
namespace {

uint32_t materialOffset(const RenderWorldSnapshot& snapshot, RenderMaterialHandle handle, MaterialCache& cache) {
    const auto* record = snapshot.material(handle);
    if (!record) {
        return cache.materialGpuOffset(0);
    }

    const std::string name = record->desc.resourceKey
                                     ? "render-material:" + std::to_string(record->desc.resourceKey.value)
                                     : "render-material-handle:" + std::to_string(handle.generation) + ":" +
                                               std::to_string(handle.index);
    const auto materialHandle = cache.registerMaterial(name, record->desc.material);
    return cache.materialGpuOffset(materialHandle);
}

Texture* loadTexture(AssetGpuRegistry& assets, const RenderTextureDesc& desc) {
    if (!desc.resourceKey || !desc.image || !desc.image->valid())
        return nullptr;

    TextureLoadOptions options;
    options.sRGB = desc.srgb;

    return assets.findTexture(desc.resourceKey, options);
}

bool hasTangentLayout(const GpuGeometry& geometry) {
    return geometry.layout.has(graphics::VertexSemantic::Tangent);
}

void populateSurfaceTextures(const RenderWorldSnapshot& snapshot, const RenderWorkItem& item, AssetGpuRegistry& assets,
                             MeshDrawCommand& command) {
    const auto* material = snapshot.material(item.material);
    if (!material)
        return;

    command.albedoTex = loadTexture(assets, material->desc.baseColorTexture);
    command.normalTex = loadTexture(assets, material->desc.normalTexture);
    command.mrTex = loadTexture(assets, material->desc.metallicRoughnessTexture);
    command.emissiveTex = loadTexture(assets, material->desc.emissiveTexture);
    command.aoTex = loadTexture(assets, material->desc.ambientOcclusionTexture);
}

MeshDrawCommand makeCommand(const RenderWorkItem& item, const RenderGeometryRecord& geometryRecord,
                            const GpuGeometry& geometry, PipelineState* pipeline, uint32_t objectOffset,
                            uint32_t materialOffset, bool isEdge) {
    MeshDrawCommand command;
    command.pipelineState = pipeline;
    command.vertexBuffer = geometry.vertexBuffer.get();
    command.indexBuffer = geometry.indexBuffer.get();
    command.indexCount = geometry.indexCount;
    command.indexType = geometry.indexType;
    command.vertexCount = geometry.vertexCount;
    command.instanceCount = 1;
    command.topology = geometryRecord.desc.topology;
    command.objectUboOffset = objectOffset;
    command.materialUboOffset = materialOffset;
    command.worldTransform = item.worldTransform;
    command.pickId = item.pickId;
    command.selected = item.selected;
    command.hovered = item.hovered;
    command.isWire = isEdge;
    return command;
}

}  // namespace

void RenderCompiler::compile(const RenderWorldSnapshot& snapshot, const RenderWorkload& workload,
                             RenderCompileContext& context) {
    clear();

    uint32_t nextObjectOffset = 0;
    const auto hasObjectUboSlot = [&]() {
        return nextObjectOffset < MeshDrawCommand::kObjectUboBytes;
    };

    for (const auto& item : workload.surfaces()) {
        ++stats_.surfaceWorkItemCount;
        if (!hasObjectUboSlot()) {
            ++stats_.objectUboLimitCount;
            break;
        }

        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord) {
            ++stats_.missingGeometryRecordCount;
            continue;
        }
        if (geometryRecord->desc.empty) {
            ++stats_.emptyGeometryCount;
            continue;
        }

        const auto* gpuGeometry = context.assets.findGeometry(geometryRecord->desc.resourceKey);
        if (!gpuGeometry) {
            ++stats_.missingGpuGeometryCount;
            continue;
        }
        if (!renderGpuGeometryMatchesBucket(item.bucket, geometryRecord->desc, *gpuGeometry)) {
            ++stats_.rejectedContractCount;
            continue;
        }

        PipelineState* surfacePipeline = hasTangentLayout(*gpuGeometry) && context.surfaceTangentPipeline
                                                 ? context.surfaceTangentPipeline
                                                 : context.surfacePipeline;
        if (!surfacePipeline) {
            ++stats_.missingPipelineCount;
            continue;
        }
        auto command = makeCommand(item, *geometryRecord, *gpuGeometry, surfacePipeline, nextObjectOffset,
                                   materialOffset(snapshot, item.material, context.materials), false);
        populateSurfaceTextures(snapshot, item, context.assets, command);
        surface_commands_.push_back(std::move(command));
        ++stats_.acceptedSurfaceCommandCount;
        nextObjectOffset += MeshDrawCommand::kObjectUboStride;
    }

    for (const auto& item : workload.edges()) {
        ++stats_.edgeWorkItemCount;
        if (!hasObjectUboSlot()) {
            ++stats_.objectUboLimitCount;
            break;
        }

        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord) {
            ++stats_.missingGeometryRecordCount;
            continue;
        }
        if (geometryRecord->desc.empty) {
            ++stats_.emptyGeometryCount;
            continue;
        }

        const auto* gpuGeometry = context.assets.findGeometry(geometryRecord->desc.resourceKey);
        if (!gpuGeometry) {
            ++stats_.missingGpuGeometryCount;
            continue;
        }
        if (!renderGpuGeometryMatchesBucket(item.bucket, geometryRecord->desc, *gpuGeometry)) {
            ++stats_.rejectedContractCount;
            continue;
        }
        if (!context.edgePipeline) {
            ++stats_.missingPipelineCount;
            continue;
        }

        edge_commands_.push_back(makeCommand(item, *geometryRecord, *gpuGeometry, context.edgePipeline,
                                             nextObjectOffset, context.materials.materialGpuOffset(0), true));
        ++stats_.acceptedEdgeCommandCount;
        nextObjectOffset += MeshDrawCommand::kObjectUboStride;
    }
}

void RenderCompiler::clear() {
    surface_commands_.clear();
    edge_commands_.clear();
    stats_.reset();
}

}  // namespace mulan::engine
