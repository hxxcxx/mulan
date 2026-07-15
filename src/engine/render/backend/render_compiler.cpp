#include "render_compiler.h"

#include "../asset_gpu_registry.h"
#include "../frontend/render_contract.h"
#include "../material/material_cache.h"
#include "../render_geometry.h"

#include <limits>
#include <optional>
#include <string>

namespace mulan::engine {
namespace {

std::optional<uint32_t> materialIndex(const RenderWorldSnapshot& snapshot, RenderMaterialHandle handle,
                                      MaterialCache& cache) {
    const auto* record = snapshot.material(handle);
    if (!record)
        return uint32_t{ 0 };

    // 默认材质有固定资源身份，直接复用 MaterialCache 的内置 0 号材质。
    if (record->desc.resourceKey == defaultRenderMaterialResourceKey()) {
        return uint32_t{ 0 };
    }

    const std::string name = record->desc.resourceKey
                                     ? "render-material:" + std::to_string(record->desc.resourceKey.value)
                                     : "render-material-handle:" + std::to_string(handle.generation) + ":" +
                                               std::to_string(handle.index);
    const MaterialHandle registered = cache.registerMaterial(name, record->desc.material);
    if (registered == kInvalidMaterialHandle || registered > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(registered);
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
                            const GpuGeometry& geometry, PipelineState* pipeline, uint32_t materialIndex, bool isEdge,
                            bool selected, bool hovered) {
    MeshDrawCommand command;
    command.pipelineState = pipeline;
    command.vertexBuffer = geometry.vertexBuffer.get();
    command.indexBuffer = geometry.indexBuffer.get();
    command.indexCount = geometry.indexCount;
    command.indexType = geometry.indexType;
    command.vertexCount = geometry.vertexCount;
    command.instanceCount = 1;
    command.topology = geometryRecord.desc.topology;
    command.materialIndex = materialIndex;
    command.worldTransform = item.worldTransform;
    command.pickId = item.pickId.valueOr(0);
    command.selected = selected;
    command.hovered = hovered;
    command.isWire = isEdge;
    return command;
}

}  // namespace

void RenderCompiler::compile(const RenderWorldSnapshot& snapshot, const RenderWorkload& workload,
                             RenderCompileContext& context) {
    clear();

    enum class CompileItemStatus {
        Accepted,
        Skipped,
    };

    const auto compileItem = [&](const RenderWorkItem& item, std::vector<MeshDrawCommand>& out, bool isEdge,
                                 bool populateTextures, bool selected, bool hovered,
                                 auto choosePipeline) -> CompileItemStatus {
        const auto* geometryRecord = snapshot.geometry(item.geometry);
        if (!geometryRecord) {
            ++stats_.missingGeometryRecordCount;
            return CompileItemStatus::Skipped;
        }
        if (geometryRecord->desc.empty) {
            ++stats_.emptyGeometryCount;
            return CompileItemStatus::Skipped;
        }

        const auto* gpuGeometry = context.assets.findGeometry(geometryRecord->desc.resourceKey);
        if (!gpuGeometry) {
            ++stats_.missingGpuGeometryCount;
            return CompileItemStatus::Skipped;
        }
        if (!renderGpuGeometryMatchesBucket(item.bucket, geometryRecord->desc, *gpuGeometry)) {
            ++stats_.rejectedContractCount;
            return CompileItemStatus::Skipped;
        }

        PipelineState* pipeline = choosePipeline(*gpuGeometry);
        if (!pipeline) {
            ++stats_.missingPipelineCount;
            return CompileItemStatus::Skipped;
        }

        const std::optional<uint32_t> resolvedMaterial = materialIndex(snapshot, item.material, context.materials);
        if (!resolvedMaterial) {
            ++stats_.materialRegistrationFailureCount;
            return CompileItemStatus::Skipped;
        }

        auto command = makeCommand(item, *geometryRecord, *gpuGeometry, pipeline, *resolvedMaterial, isEdge, selected,
                                   hovered);
        if (populateTextures) {
            populateSurfaceTextures(snapshot, item, context.assets, command);
        }
        out.push_back(std::move(command));
        return CompileItemStatus::Accepted;
    };

    for (const auto& item : workload.surfaces()) {
        ++stats_.surfaceWorkItemCount;
        const auto status =
                compileItem(item, surface_commands_, false, true, false, false, [&](const GpuGeometry& gpuGeometry) {
                    return hasTangentLayout(gpuGeometry) && context.surfaceTangentPipeline
                                   ? context.surfaceTangentPipeline
                                   : context.surfacePipeline;
                });
        if (status == CompileItemStatus::Accepted)
            ++stats_.acceptedSurfaceCommandCount;
    }

    for (const auto& item : workload.edges()) {
        ++stats_.edgeWorkItemCount;
        const auto status = compileItem(item, edge_commands_, true, false, false, false,
                                        [&](const GpuGeometry&) { return context.edgePipeline; });
        if (status == CompileItemStatus::Accepted)
            ++stats_.acceptedEdgeCommandCount;
    }

    for (const auto& item : workload.highlightSurfaces()) {
        ++stats_.highlightSurfaceWorkItemCount;
        const auto status =
                compileItem(item, highlight_surface_commands_, false, false, item.selected, item.hovered,
                            [&](const GpuGeometry& gpuGeometry) {
                                return hasTangentLayout(gpuGeometry) && context.highlightSurfaceTangentPipeline
                                               ? context.highlightSurfaceTangentPipeline
                                               : context.highlightSurfacePipeline;
                            });
        if (status == CompileItemStatus::Accepted)
            ++stats_.acceptedHighlightSurfaceCommandCount;
    }

    for (const auto& item : workload.highlightEdges()) {
        ++stats_.highlightEdgeWorkItemCount;
        const auto status = compileItem(item, highlight_edge_commands_, true, false, item.selected, item.hovered,
                                        [&](const GpuGeometry&) { return context.highlightEdgePipeline; });
        if (status == CompileItemStatus::Accepted)
            ++stats_.acceptedHighlightEdgeCommandCount;
    }
}

void RenderCompiler::clear() {
    surface_commands_.clear();
    edge_commands_.clear();
    highlight_surface_commands_.clear();
    highlight_edge_commands_.clear();
    stats_.reset();
}

}  // namespace mulan::engine
