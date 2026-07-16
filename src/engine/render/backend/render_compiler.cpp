#include "render_compiler.h"

#include "../asset_gpu_registry.h"
#include "../frontend/render_contract.h"
#include "../material/material_cache.h"
#include "../render_geometry.h"

#include <limits>
#include <optional>
#include <string>
#include <algorithm>
#include <functional>

namespace mulan::engine {
namespace {

uint64_t mixSortKey(uint64_t seed, uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

uint64_t pointerSortKey(const void* pointer) noexcept {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

void updateSortKey(MeshDrawCommand& command) noexcept {
    uint64_t key = 0xcbf29ce484222325ULL;
    key = mixSortKey(key, pointerSortKey(command.pipelineState));
    key = mixSortKey(key, pointerSortKey(command.albedoTex));
    key = mixSortKey(key, pointerSortKey(command.normalTex));
    key = mixSortKey(key, pointerSortKey(command.mrTex));
    key = mixSortKey(key, pointerSortKey(command.emissiveTex));
    key = mixSortKey(key, pointerSortKey(command.aoTex));
    key = mixSortKey(key, pointerSortKey(command.sampler));
    key = mixSortKey(key, command.materialIndex);
    key = mixSortKey(key, pointerSortKey(command.vertexBuffer));
    key = mixSortKey(key, pointerSortKey(command.indexBuffer));
    key = mixSortKey(key, static_cast<uint64_t>(command.indexType));
    command.sortKey = key;
}

template <typename T>
int compareValue(const T& lhs, const T& rhs) noexcept {
    if (std::less<T>{}(lhs, rhs))
        return -1;
    if (std::less<T>{}(rhs, lhs))
        return 1;
    return 0;
}

bool opaqueCommandLess(const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) noexcept {
    // 绑定成本从高到低排列。指针只作为当前 Device 生命周期内的资源身份，
    // std::less 对无关对象指针提供严格全序，不依赖未定义的原生指针比较。
    if (const int order = compareValue(lhs.pipelineState, rhs.pipelineState); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.albedoTex, rhs.albedoTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.normalTex, rhs.normalTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.mrTex, rhs.mrTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.emissiveTex, rhs.emissiveTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.aoTex, rhs.aoTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.sampler, rhs.sampler); order != 0)
        return order < 0;
    if (lhs.materialIndex != rhs.materialIndex)
        return lhs.materialIndex < rhs.materialIndex;
    if (const int order = compareValue(lhs.vertexBuffer, rhs.vertexBuffer); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.indexBuffer, rhs.indexBuffer); order != 0)
        return order < 0;
    if (lhs.indexType != rhs.indexType)
        return static_cast<uint8_t>(lhs.indexType) < static_cast<uint8_t>(rhs.indexType);
    if (lhs.firstIndex != rhs.firstIndex)
        return lhs.firstIndex < rhs.firstIndex;
    if (lhs.baseVertex != rhs.baseVertex)
        return lhs.baseVertex < rhs.baseVertex;
    return lhs.pickId < rhs.pickId;
}

void sortDrawCommands(std::vector<MeshDrawCommand>& commands) {
    for (auto& command : commands)
        updateSortKey(command);

    // 透明命令必须保留上层提供的深度顺序。当前管线尚未产生透明命令，
    // 但在排序入口预先守住该契约，避免以后新增透明材质时出现视觉回归。
    const auto opaqueEnd = std::stable_partition(commands.begin(), commands.end(),
                                                 [](const auto& command) { return !command.translucent; });
    std::sort(commands.begin(), opaqueEnd, opaqueCommandLess);
}

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
                                     ? "render-material:" + std::to_string(record->desc.resourceKey.domain.value) +
                                               ":" + std::to_string(record->desc.resourceKey.source) + ":" +
                                               std::to_string(record->desc.resourceKey.subresource)
                                     : "render-material-handle:" + std::to_string(handle.generation) + ":" +
                                               std::to_string(handle.index);
    const MaterialHandle registered = cache.registerMaterial(name, record->desc.material);
    if (registered == kInvalidMaterialHandle || registered > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(registered);
}

Texture* loadTexture(AssetGpuRegistry& assets, const RenderTextureDesc& desc, RenderCompilerStats& stats) {
    if (!desc.resourceKey || !desc.image || !desc.image->valid())
        return nullptr;

    TextureLoadOptions options;
    options.sRGB = desc.srgb;
    options.generateMips = desc.generateMips;

    Texture* texture = assets.findTexture(desc.resourceKey, options);
    if (!texture) {
        ++stats.missingGpuTextureCount;
    }
    return texture;
}

bool hasTangentLayout(const GpuGeometry& geometry) {
    return geometry.layout.has(graphics::VertexSemantic::Tangent);
}

void populateSurfaceTextures(const RenderWorldSnapshot& snapshot, const RenderWorkItem& item, AssetGpuRegistry& assets,
                             RenderCompilerStats& stats, MeshDrawCommand& command) {
    const auto* material = snapshot.material(item.material);
    if (!material)
        return;

    command.albedoTex = loadTexture(assets, material->desc.baseColorTexture, stats);
    command.normalTex = loadTexture(assets, material->desc.normalTexture, stats);
    command.mrTex = loadTexture(assets, material->desc.metallicRoughnessTexture, stats);
    command.emissiveTex = loadTexture(assets, material->desc.emissiveTexture, stats);
    command.aoTex = loadTexture(assets, material->desc.ambientOcclusionTexture, stats);
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
            populateSurfaceTextures(snapshot, item, context.assets, stats_, command);
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

    sortDrawCommands(surface_commands_);
    sortDrawCommands(edge_commands_);
    // Highlight pass 使用 alpha blend，覆盖顺序属于视觉语义；保持 workload 顺序。
    for (auto& command : highlight_surface_commands_)
        updateSortKey(command);
    for (auto& command : highlight_edge_commands_)
        updateSortKey(command);
}

void RenderCompiler::clear() {
    surface_commands_.clear();
    edge_commands_.clear();
    highlight_surface_commands_.clear();
    highlight_edge_commands_.clear();
    stats_.reset();
}

}  // namespace mulan::engine
