#include "render_compiler.h"

#include "../asset_gpu_registry.h"
#include "../frontend/render_contract.h"
#include "../material/material_cache.h"
#include "../render_geometry.h"
#include "../../rhi/engine_error_code.h"

#include <mulan/math/spatial/dynamic_bvh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mulan::engine {
namespace {

using ObjectIdHash = RenderHandleHash<RenderObjectIdTag>;
using GeometryHandleHash = RenderHandleHash<GeometryHandleTag>;
using MaterialHandleHash = RenderHandleHash<MaterialHandleTag>;

PipelineState* selectSurfaceRasterPipeline(PipelineState* regular, PipelineState* doubleSided, PipelineState* mirrored,
                                           bool materialDoubleSided, const math::Mat4& world) noexcept {
    if (materialDoubleSided)
        return doubleSided ? doubleSided : regular;

    const math::Mat3 linear(world);
    const double determinant = linear.determinant();
    const double scale = linear[0].length() * linear[1].length() * linear[2].length();
    if (!std::isfinite(determinant) || !std::isfinite(scale) || scale <= 0.0 ||
        std::abs(determinant) <= std::numeric_limits<double>::epsilon() * 64.0 * scale) {
        return doubleSided ? doubleSided : regular;
    }
    return determinant < 0.0 ? (mirrored ? mirrored : (doubleSided ? doubleSided : regular)) : regular;
}

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
    key = mixSortKey(key, command.indexCount);
    key = mixSortKey(key, command.firstIndex);
    key = mixSortKey(key, static_cast<uint64_t>(static_cast<int64_t>(command.baseVertex)));
    key = mixSortKey(key, command.vertexCount);
    key = mixSortKey(key, command.instanceCount);
    key = mixSortKey(key, static_cast<uint64_t>(command.topology));
    key = mixSortKey(key, command.isWire ? 1u : 0u);
    key = mixSortKey(key, command.batchInstancingEligible ? 1u : 0u);
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
    // 绑定成本从高到低排列。std::less 为无关对象指针提供严格全序。
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
    if (lhs.indexCount != rhs.indexCount)
        return lhs.indexCount < rhs.indexCount;
    if (lhs.firstIndex != rhs.firstIndex)
        return lhs.firstIndex < rhs.firstIndex;
    if (lhs.baseVertex != rhs.baseVertex)
        return lhs.baseVertex < rhs.baseVertex;
    if (lhs.vertexCount != rhs.vertexCount)
        return lhs.vertexCount < rhs.vertexCount;
    if (lhs.instanceCount != rhs.instanceCount)
        return lhs.instanceCount < rhs.instanceCount;
    if (lhs.topology != rhs.topology)
        return static_cast<uint8_t>(lhs.topology) < static_cast<uint8_t>(rhs.topology);
    if (lhs.isWire != rhs.isWire)
        return lhs.isWire < rhs.isWire;
    if (lhs.batchInstancingEligible != rhs.batchInstancingEligible)
        return lhs.batchInstancingEligible < rhs.batchInstancingEligible;
    return lhs.pickId < rhs.pickId;
}

void sortDrawCommands(std::vector<MeshDrawCommand>& commands) {
    for (auto& command : commands)
        updateSortKey(command);

    // 透明命令保留上层稳定顺序，避免破坏后续的深度排序契约。
    const auto opaqueEnd = std::stable_partition(commands.begin(), commands.end(),
                                                 [](const auto& command) { return !command.translucent; });
    std::sort(commands.begin(), opaqueEnd, opaqueCommandLess);
}

bool objectIdLess(RenderObjectId lhs, RenderObjectId rhs) noexcept {
    return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
}

template <typename Handle>
void appendUnique(std::vector<Handle>& handles, Handle handle) {
    if (std::find(handles.begin(), handles.end(), handle) == handles.end())
        handles.push_back(handle);
}

bool matricesEqual(const math::Mat4& lhs, const math::Mat4& rhs) noexcept {
    for (int i = 0; i < 16; ++i) {
        if (lhs.data()[i] != rhs.data()[i])
            return false;
    }
    return true;
}

bool selectionTargetEqual(const SelectionVisualTarget& lhs, const SelectionVisualTarget& rhs) noexcept {
    return lhs.pickId == rhs.pickId && lhs.role == rhs.role && lhs.domain == rhs.domain &&
           lhs.sourceDrawableIndex == rhs.sourceDrawableIndex &&
           lhs.hasSourceDrawableIndex == rhs.hasSourceDrawableIndex && lhs.primitiveIndex == rhs.primitiveIndex &&
           lhs.hasPrimitiveIndex == rhs.hasPrimitiveIndex && lhs.componentIndex == rhs.componentIndex &&
           lhs.hasComponentIndex == rhs.hasComponentIndex;
}

bool commandOptionsEqual(const RenderOptions& lhs, const RenderOptions& rhs) noexcept {
    if (lhs.displayMode != rhs.displayMode || lhs.surfaceTechnique != rhs.surfaceTechnique ||
        lhs.hoveredPickId != rhs.hoveredPickId || lhs.showSurfaces != rhs.showSurfaces ||
        lhs.showEdges != rhs.showEdges || lhs.showOverlays != rhs.showOverlays ||
        lhs.selectionVisuals.active() != rhs.selectionVisuals.active()) {
        return false;
    }
    const auto lhsTargets = lhs.selectionVisuals.targets();
    const auto rhsTargets = rhs.selectionVisuals.targets();
    return lhsTargets.size() == rhsTargets.size() &&
           std::equal(lhsTargets.begin(), lhsTargets.end(), rhsTargets.begin(), selectionTargetEqual);
}

bool indexableBounds(const math::AABB3& bounds) {
    return math::DynamicBVH<RenderObjectId, ObjectIdHash>::isValidBounds(bounds) && !bounds.isEmpty();
}

math::AABB3 paddedFrustumBounds(const math::AABB3& bounds) {
    math::AABB3 padded = bounds;
    const double amount = math::defaultTolerance().lengthEps;
    const math::Vec3 padding(amount, amount, amount);
    padded.min -= padding;
    padded.max += padding;
    return padded;
}

enum class DrawableStatus : uint8_t {
    Ready,
    MissingGeometryRecord,
    EmptyGeometry,
    MissingGpuGeometry,
    MissingGpuTexture,
    RejectedContract,
    MaterialRegistrationFailure,
};

struct CachedRenderDrawable {
    RenderBucket bucket = RenderBucket::Surface;
    size_t sourceDrawableIndex = 0;
    /// 保持旧编译顺序：geometry/contract → pipeline → material → base texture。
    DrawableStatus geometryStatus = DrawableStatus::Ready;
    DrawableStatus materialStatus = DrawableStatus::Ready;
    /// 纹理只属于 surface base pass；highlight 不应被缺失纹理连带阻断。
    DrawableStatus baseTextureStatus = DrawableStatus::Ready;
    size_t missingGpuTextureCount = 0;
    RenderMaterialHandle materialHandle;
    MeshDrawCommand baseCommand;
    PipelineState* highlightPipeline = nullptr;
};

struct RenderPacket {
    RenderObjectId id;
    PickId pickId;
    math::AABB3 worldBounds;
    bool sourceVisible = false;
    bool defaultSelected = false;
    std::vector<CachedRenderDrawable> drawables;
    std::vector<GeometryHandle> geometryDependencies;
    std::vector<RenderMaterialHandle> materialDependencies;
    std::vector<RenderResourceKey> assetDependencies;
};

struct ResolvedGeometry {
    const RenderGeometryRecord* record = nullptr;
    const GpuGeometry* gpu = nullptr;
    DrawableStatus status = DrawableStatus::Ready;
};

struct ResolvedMaterial {
    const RenderMaterialRecord* record = nullptr;
    uint32_t materialIndex = 0;
    bool registrationResolved = false;
    bool registrationFailed = false;
    bool texturesResolved = false;
    bool texturesAvailable = true;
    size_t missingTextureCount = 0;
    Texture* albedo = nullptr;
    Texture* normal = nullptr;
    Texture* metallicRoughness = nullptr;
    Texture* emissive = nullptr;
    Texture* ambientOcclusion = nullptr;
};

struct ContextIdentity {
    const AssetGpuRegistry* assets = nullptr;
    const MaterialCache* materials = nullptr;
    uint64_t materialLayoutRevision = 0;
    PipelineState* surfacePipeline = nullptr;
    PipelineState* surfaceDoubleSidedPipeline = nullptr;
    PipelineState* surfaceMirroredPipeline = nullptr;
    PipelineState* surfaceTangentPipeline = nullptr;
    PipelineState* surfaceTangentDoubleSidedPipeline = nullptr;
    PipelineState* surfaceTangentMirroredPipeline = nullptr;
    PipelineState* edgePipeline = nullptr;
    PipelineState* highlightSurfacePipeline = nullptr;
    PipelineState* highlightSurfaceTangentPipeline = nullptr;
    PipelineState* highlightEdgePipeline = nullptr;

    bool matches(const RenderCompileContext& context) const noexcept {
        return assets == &context.assets && materials == &context.materials &&
               materialLayoutRevision == context.materials.layoutRevision() &&
               surfacePipeline == context.surfacePipeline &&
               surfaceDoubleSidedPipeline == context.surfaceDoubleSidedPipeline &&
               surfaceMirroredPipeline == context.surfaceMirroredPipeline &&
               surfaceTangentPipeline == context.surfaceTangentPipeline &&
               surfaceTangentDoubleSidedPipeline == context.surfaceTangentDoubleSidedPipeline &&
               surfaceTangentMirroredPipeline == context.surfaceTangentMirroredPipeline &&
               edgePipeline == context.edgePipeline && highlightSurfacePipeline == context.highlightSurfacePipeline &&
               highlightSurfaceTangentPipeline == context.highlightSurfaceTangentPipeline &&
               highlightEdgePipeline == context.highlightEdgePipeline;
    }

    static ContextIdentity capture(const RenderCompileContext& context) noexcept {
        return { .assets = &context.assets,
                 .materials = &context.materials,
                 .materialLayoutRevision = context.materials.layoutRevision(),
                 .surfacePipeline = context.surfacePipeline,
                 .surfaceDoubleSidedPipeline = context.surfaceDoubleSidedPipeline,
                 .surfaceMirroredPipeline = context.surfaceMirroredPipeline,
                 .surfaceTangentPipeline = context.surfaceTangentPipeline,
                 .surfaceTangentDoubleSidedPipeline = context.surfaceTangentDoubleSidedPipeline,
                 .surfaceTangentMirroredPipeline = context.surfaceTangentMirroredPipeline,
                 .edgePipeline = context.edgePipeline,
                 .highlightSurfacePipeline = context.highlightSurfacePipeline,
                 .highlightSurfaceTangentPipeline = context.highlightSurfaceTangentPipeline,
                 .highlightEdgePipeline = context.highlightEdgePipeline };
    }
};

void recordDrawableFailure(DrawableStatus status, RenderCompilerStats& stats) {
    switch (status) {
    case DrawableStatus::Ready: break;
    case DrawableStatus::MissingGeometryRecord: ++stats.missingGeometryRecordCount; break;
    case DrawableStatus::EmptyGeometry: ++stats.emptyGeometryCount; break;
    case DrawableStatus::MissingGpuGeometry: ++stats.missingGpuGeometryCount; break;
    case DrawableStatus::MissingGpuTexture: ++stats.missingGpuTextureCount; break;
    case DrawableStatus::RejectedContract: ++stats.rejectedContractCount; break;
    case DrawableStatus::MaterialRegistrationFailure: ++stats.materialRegistrationFailureCount; break;
    }
}

bool bucketEnabled(RenderBucket bucket, const RenderOptions& options, RenderWorkloadStats& stats) {
    if (renderBucketIsOverlay(bucket)) {
        if (!options.showOverlays) {
            ++stats.skippedDisabledOverlayCount;
            return false;
        }
        return true;
    }

    switch (renderBucketPass(bucket)) {
    case RenderPassKind::Surface:
        if (!renderSurfacesEnabled(options)) {
            ++stats.skippedDisabledSurfaceCount;
            return false;
        }
        return true;
    case RenderPassKind::Edge:
        if (!renderEdgesEnabled(options)) {
            ++stats.skippedDisabledEdgeCount;
            return false;
        }
        return true;
    case RenderPassKind::None: ++stats.skippedUnsupportedBucketCount; return false;
    }
    ++stats.skippedUnsupportedBucketCount;
    return false;
}

bool bucketWillEmitBase(RenderBucket bucket, const RenderOptions& options) {
    if (renderBucketIsOverlay(bucket))
        return options.showOverlays;
    switch (renderBucketPass(bucket)) {
    case RenderPassKind::Surface: return renderSurfacesEnabled(options);
    case RenderPassKind::Edge: return renderEdgesEnabled(options);
    case RenderPassKind::None: return false;
    }
    return false;
}

}  // namespace

struct RenderCompiler::Impl {
    using PacketMap = std::unordered_map<RenderObjectId, RenderPacket, ObjectIdHash>;
    using ObjectSet = std::unordered_set<RenderObjectId, ObjectIdHash>;
    using GeometrySet = std::unordered_set<GeometryHandle, GeometryHandleHash>;
    using MaterialSet = std::unordered_set<RenderMaterialHandle, MaterialHandleHash>;
    using GeometryResolutionMap = std::unordered_map<GeometryHandle, ResolvedGeometry, GeometryHandleHash>;
    using MaterialResolutionMap = std::unordered_map<RenderMaterialHandle, ResolvedMaterial, MaterialHandleHash>;
    using GeometryDependents = std::unordered_map<GeometryHandle, ObjectSet, GeometryHandleHash>;
    using MaterialDependents = std::unordered_map<RenderMaterialHandle, ObjectSet, MaterialHandleHash>;
    using AssetDependents = std::unordered_map<RenderResourceKey, ObjectSet>;
    using GeometryResolutionsByAsset = std::unordered_map<RenderResourceKey, GeometrySet>;
    using MaterialResolutionsByTexture = std::unordered_map<RenderResourceKey, MaterialSet>;
    using SpatialIndex = math::DynamicBVH<RenderObjectId, ObjectIdHash>;

    struct ResolutionTransaction {
        bool allowPersistent = true;
        const GeometrySet* invalidGeometries = nullptr;
        const MaterialSet* invalidMaterials = nullptr;
        GeometryResolutionMap geometries;
        MaterialResolutionMap materials;
    };

    struct VisibilityCache {
        bool valid = false;
        math::Mat4 view;
        math::Mat4 projection;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failOpen = false;
        SpatialIndex::QueryStats queryStats;
        std::vector<RenderObjectId> ids;
    };

    std::optional<RenderWorldSnapshot> snapshot;
    ContextIdentity contextIdentity;
    PacketMap packets;
    GeometryResolutionMap resolvedGeometries;
    MaterialResolutionMap resolvedMaterials;
    GeometryDependents geometryDependents;
    MaterialDependents materialDependents;
    AssetDependents assetDependents;
    // Resolution 的寿命可以超过当前 Packet。独立反向索引保证“暂时没有对象使用”时
    // 发生的 GPU 资源变化仍会驱逐旧指针，不能只依赖 Packet 的 assetDependents。
    GeometryResolutionsByAsset geometryResolutionsByAsset;
    MaterialResolutionsByTexture materialResolutionsByTexture;
    AssetGpuChangeCursor assetChangeCursor;
    SpatialIndex spatialIndex;
    ObjectSet uncullableObjects;
    ObjectSet unresolvedPackets;
    size_t sourceVisibleObjectCount = 0;
    VisibilityCache visibilityCache;
    uint64_t packetContentRevision = 0;
    uint64_t assembledPacketContentRevision = 0;
    std::optional<RenderOptions> assembledOptions;
    std::vector<RenderObjectId> assembledVisibleIds;

    std::vector<MeshDrawCommand> surfaceCommands;
    std::vector<MeshDrawCommand> edgeCommands;
    std::vector<MeshDrawCommand> highlightSurfaceCommands;
    std::vector<MeshDrawCommand> highlightEdgeCommands;
    // 事务组装双缓冲：失败不污染已发布命令，成功后旧容量留在 scratch 供下帧复用。
    std::vector<MeshDrawCommand> scratchSurfaceCommands;
    std::vector<MeshDrawCommand> scratchEdgeCommands;
    std::vector<MeshDrawCommand> scratchHighlightSurfaceCommands;
    std::vector<MeshDrawCommand> scratchHighlightEdgeCommands;
    RenderCompilerStats stats;
    RenderWorkloadStats workloadStats;
    RenderPacketCacheStats packetStats;
    uint64_t commandRevision = 1;

    void advancePacketContentRevision() noexcept {
        if (++packetContentRevision == 0)
            ++packetContentRevision;
    }

    void advanceCommandRevision() noexcept {
        if (++commandRevision == 0)
            ++commandRevision;
    }

    void invalidateAssemblyKey() {
        assembledOptions.reset();
        assembledVisibleIds.clear();
        assembledPacketContentRevision = 0;
    }

    static void attachGeometryResolution(GeometryHandle handle, const ResolvedGeometry& resolution,
                                         GeometryResolutionsByAsset& byAsset) {
        if (resolution.record && resolution.record->desc.resourceKey)
            byAsset[resolution.record->desc.resourceKey].insert(handle);
    }

    static void detachGeometryResolution(GeometryHandle handle, const ResolvedGeometry& resolution,
                                         GeometryResolutionsByAsset& byAsset) {
        if (!resolution.record || !resolution.record->desc.resourceKey)
            return;
        auto known = byAsset.find(resolution.record->desc.resourceKey);
        if (known == byAsset.end())
            return;
        known->second.erase(handle);
        if (known->second.empty())
            byAsset.erase(known);
    }

    template <typename Visitor>
    static void forEachResolvedTextureKey(const ResolvedMaterial& resolution, Visitor&& visitor) {
        // texturesResolved=false 的缓存没有保存任何 Texture*，无需因纹理事件失效。
        if (!resolution.record || !resolution.texturesResolved)
            return;
        const RenderTextureDesc* textures[] = { &resolution.record->desc.baseColorTexture,
                                                &resolution.record->desc.normalTexture,
                                                &resolution.record->desc.metallicRoughnessTexture,
                                                &resolution.record->desc.emissiveTexture,
                                                &resolution.record->desc.ambientOcclusionTexture };
        for (const RenderTextureDesc* texture : textures) {
            if (texture->resourceKey)
                std::invoke(visitor, texture->resourceKey);
        }
    }

    static void attachMaterialResolution(RenderMaterialHandle handle, const ResolvedMaterial& resolution,
                                         MaterialResolutionsByTexture& byTexture) {
        forEachResolvedTextureKey(resolution, [&](RenderResourceKey key) { byTexture[key].insert(handle); });
    }

    static void detachMaterialResolution(RenderMaterialHandle handle, const ResolvedMaterial& resolution,
                                         MaterialResolutionsByTexture& byTexture) {
        forEachResolvedTextureKey(resolution, [&](RenderResourceKey key) {
            auto known = byTexture.find(key);
            if (known == byTexture.end())
                return;
            known->second.erase(handle);
            if (known->second.empty())
                byTexture.erase(known);
        });
    }

    ResolvedGeometry& resolveGeometry(const RenderWorldSnapshot& current, GeometryHandle handle,
                                      RenderCompileContext& context, ResolutionTransaction& transaction) {
        if (auto known = transaction.geometries.find(handle); known != transaction.geometries.end())
            return known->second;

        const bool invalidated = transaction.invalidGeometries && transaction.invalidGeometries->contains(handle);
        if (transaction.allowPersistent && !invalidated) {
            if (auto known = resolvedGeometries.find(handle); known != resolvedGeometries.end())
                return transaction.geometries.emplace(handle, known->second).first->second;
        }

        ResolvedGeometry resolved;
        resolved.record = current.geometry(handle);
        if (!resolved.record) {
            resolved.status = DrawableStatus::MissingGeometryRecord;
        } else if (resolved.record->desc.empty) {
            resolved.status = DrawableStatus::EmptyGeometry;
        } else {
            resolved.gpu = context.assets.findGeometry(resolved.record->desc.resourceKey);
            if (!resolved.gpu)
                resolved.status = DrawableStatus::MissingGpuGeometry;
        }
        return transaction.geometries.emplace(handle, resolved).first->second;
    }

    ResolvedMaterial& resolveMaterial(const RenderWorldSnapshot& current, RenderMaterialHandle handle,
                                      ResolutionTransaction& transaction) {
        if (auto known = transaction.materials.find(handle); known != transaction.materials.end())
            return known->second;

        const bool invalidated = transaction.invalidMaterials && transaction.invalidMaterials->contains(handle);
        if (transaction.allowPersistent && !invalidated) {
            if (auto known = resolvedMaterials.find(handle); known != resolvedMaterials.end())
                return transaction.materials.emplace(handle, known->second).first->second;
        }

        ResolvedMaterial resolved;
        resolved.record = current.material(handle);
        if (!resolved.record) {
            resolved.registrationResolved = true;
            resolved.texturesResolved = true;
            return transaction.materials.emplace(handle, resolved).first->second;
        }

        if (resolved.record->desc.resourceKey == defaultRenderMaterialResourceKey()) {
            resolved.registrationResolved = true;
            return transaction.materials.emplace(handle, resolved).first->second;
        }
        return transaction.materials.emplace(handle, resolved).first->second;
    }

    void ensureMaterialRegistration(ResolvedMaterial& material, RenderMaterialHandle handle, uint64_t world,
                                    RenderCompileContext& context) {
        if (material.registrationResolved)
            return;
        material.registrationResolved = true;
        if (!material.record)
            return;

        const std::string name =
                material.record->desc.resourceKey
                        ? "render-material:" + std::to_string(material.record->desc.resourceKey.domain.value) + ":" +
                                  std::to_string(material.record->desc.resourceKey.source) + ":" +
                                  std::to_string(material.record->desc.resourceKey.subresource) + ":" +
                                  std::to_string(static_cast<uint8_t>(material.record->desc.resourceKey.kind))
                        : "render-material-world:" + std::to_string(world) + ":" + std::to_string(handle.generation) +
                                  ":" + std::to_string(handle.index);
        const MaterialHandle registered = context.materials.registerMaterial(name, material.record->desc.material);
        if (registered == kInvalidMaterialHandle || registered > std::numeric_limits<uint32_t>::max()) {
            material.registrationFailed = true;
        } else {
            material.materialIndex = static_cast<uint32_t>(registered);
        }
    }

    bool resolveSurfaceTextures(ResolvedMaterial& material, RenderCompileContext& context) {
        if (material.texturesResolved)
            return material.texturesAvailable;
        material.texturesResolved = true;
        if (!material.record)
            return true;

        const auto load = [&](const RenderTextureDesc& desc) -> Texture* {
            if (!desc.resourceKey || !desc.image || !desc.image->valid())
                return nullptr;
            TextureLoadOptions options;
            options.sRGB = desc.srgb;
            options.generateMips = desc.generateMips;
            Texture* texture = context.assets.findTexture(desc.resourceKey, options);
            if (!texture) {
                material.texturesAvailable = false;
                ++material.missingTextureCount;
            }
            return texture;
        };

        material.albedo = load(material.record->desc.baseColorTexture);
        material.normal = load(material.record->desc.normalTexture);
        material.metallicRoughness = load(material.record->desc.metallicRoughnessTexture);
        material.emissive = load(material.record->desc.emissiveTexture);
        material.ambientOcclusion = load(material.record->desc.ambientOcclusionTexture);
        return material.texturesAvailable;
    }

    RenderPacket compilePacket(const RenderWorldSnapshot& current, const RenderObjectRecord& object,
                               RenderCompileContext& context, ResolutionTransaction& transaction) {
        RenderPacket packet;
        packet.id = object.id;
        packet.pickId = object.desc.pickId;
        packet.worldBounds = object.desc.worldBounds;
        packet.sourceVisible = object.desc.visible;
        packet.defaultSelected = object.desc.selected;
        if (!packet.sourceVisible)
            return packet;

        packet.drawables.reserve(object.desc.drawables.size());
        for (const RenderObjectDrawable& drawable : object.desc.drawables) {
            CachedRenderDrawable cached;
            cached.bucket = drawable.bucket;
            cached.sourceDrawableIndex = drawable.sourceDrawableIndex;
            cached.materialHandle = drawable.material;

            // Gizmo/Text 当前不由 MeshDrawCommand 管线消费。保持它们在 workload
            // 统计中的可见性，但不能因未准备一份实际不会绘制的资源而拒绝整帧。
            if (renderBucketPass(drawable.bucket) == RenderPassKind::None) {
                packet.drawables.push_back(std::move(cached));
                continue;
            }

            if (drawable.geometry.isValid())
                appendUnique(packet.geometryDependencies, drawable.geometry);
            if (drawable.material.isValid())
                appendUnique(packet.materialDependencies, drawable.material);

            ResolvedGeometry& geometry = resolveGeometry(current, drawable.geometry, context, transaction);
            if (geometry.record && geometry.record->desc.resourceKey)
                appendUnique(packet.assetDependencies, geometry.record->desc.resourceKey);
            if (geometry.status != DrawableStatus::Ready) {
                cached.geometryStatus = geometry.status;
                packet.drawables.push_back(std::move(cached));
                continue;
            }
            if (!renderGpuGeometryMatchesBucket(drawable.bucket, geometry.record->desc, *geometry.gpu)) {
                cached.geometryStatus = DrawableStatus::RejectedContract;
                packet.drawables.push_back(std::move(cached));
                continue;
            }

            const bool tangentLayout = geometry.gpu->layout.has(graphics::VertexSemantic::Tangent);
            switch (renderBucketPass(drawable.bucket)) {
            case RenderPassKind::Surface:
                cached.baseCommand.pipelineState = tangentLayout && context.surfaceTangentPipeline
                                                           ? context.surfaceTangentPipeline
                                                           : context.surfacePipeline;
                cached.highlightPipeline = tangentLayout && context.highlightSurfaceTangentPipeline
                                                   ? context.highlightSurfaceTangentPipeline
                                                   : context.highlightSurfacePipeline;
                break;
            case RenderPassKind::Edge:
                cached.baseCommand.pipelineState = context.edgePipeline;
                cached.highlightPipeline = context.highlightEdgePipeline;
                break;
            case RenderPassKind::None: break;
            }

            ResolvedMaterial& material = resolveMaterial(current, drawable.material, transaction);
            if (renderBucketPass(drawable.bucket) == RenderPassKind::Surface) {
                const bool materialDoubleSided = material.record && material.record->desc.material.doubleSided;
                if (tangentLayout) {
                    cached.baseCommand.pipelineState = selectSurfaceRasterPipeline(
                            context.surfaceTangentPipeline, context.surfaceTangentDoubleSidedPipeline,
                            context.surfaceTangentMirroredPipeline, materialDoubleSided, object.desc.worldTransform);
                } else {
                    cached.baseCommand.pipelineState = selectSurfaceRasterPipeline(
                            context.surfacePipeline, context.surfaceDoubleSidedPipeline,
                            context.surfaceMirroredPipeline, materialDoubleSided, object.desc.worldTransform);
                }
            }
            if (material.record) {
                const RenderTextureDesc* textures[] = { &material.record->desc.baseColorTexture,
                                                        &material.record->desc.normalTexture,
                                                        &material.record->desc.metallicRoughnessTexture,
                                                        &material.record->desc.emissiveTexture,
                                                        &material.record->desc.ambientOcclusionTexture };
                for (const RenderTextureDesc* texture : textures) {
                    if (texture->resourceKey)
                        appendUnique(packet.assetDependencies, texture->resourceKey);
                }
            }
            if (renderBucketPass(drawable.bucket) == RenderPassKind::Surface &&
                !resolveSurfaceTextures(material, context)) {
                cached.baseTextureStatus = DrawableStatus::MissingGpuTexture;
                cached.missingGpuTextureCount = material.missingTextureCount;
            }

            MeshDrawCommand& command = cached.baseCommand;
            command.vertexBuffer = geometry.gpu->vertexBuffer.get();
            command.indexBuffer = geometry.gpu->indexBuffer.get();
            command.indexCount = geometry.gpu->indexCount;
            command.indexType = geometry.gpu->indexType;
            command.vertexCount = geometry.gpu->vertexCount;
            command.instanceCount = 1;
            command.topology = geometry.record->desc.topology;
            command.materialIndex = material.materialIndex;
            command.worldTransform = object.desc.worldTransform;
            command.pickId = object.desc.pickId.valueOr(0);
            command.isWire = renderBucketPass(drawable.bucket) == RenderPassKind::Edge;
            if (renderBucketPass(drawable.bucket) == RenderPassKind::Surface) {
                command.albedoTex = material.albedo;
                command.normalTex = material.normal;
                command.mrTex = material.metallicRoughness;
                command.emissiveTex = material.emissive;
                command.aoTex = material.ambientOcclusion;
            }
            packet.drawables.push_back(std::move(cached));
        }
        return packet;
    }

    void finalizePacketMaterials(PacketMap& candidatePackets, std::span<const RenderObjectId> orderedIds,
                                 uint64_t world, RenderCompileContext& context, ResolutionTransaction& transaction) {
        for (RenderObjectId id : orderedIds) {
            auto candidate = candidatePackets.find(id);
            if (candidate == candidatePackets.end())
                continue;
            RenderPacket& packet = candidate->second;
            for (CachedRenderDrawable& drawable : packet.drawables) {
                if (drawable.geometryStatus != DrawableStatus::Ready)
                    continue;
                const auto known = transaction.materials.find(drawable.materialHandle);
                if (known == transaction.materials.end())
                    continue;
                ResolvedMaterial& material = known->second;
                ensureMaterialRegistration(material, drawable.materialHandle, world, context);
                if (material.registrationFailed) {
                    drawable.materialStatus = DrawableStatus::MaterialRegistrationFailure;
                    continue;
                }
                drawable.baseCommand.materialIndex = material.materialIndex;
            }
        }
    }

    static void attachDependencies(const RenderPacket& packet, GeometryDependents& geometryUsers,
                                   MaterialDependents& materialUsers, AssetDependents& assetUsers) {
        for (GeometryHandle handle : packet.geometryDependencies)
            geometryUsers[handle].insert(packet.id);
        for (RenderMaterialHandle handle : packet.materialDependencies)
            materialUsers[handle].insert(packet.id);
        for (RenderResourceKey key : packet.assetDependencies)
            assetUsers[key].insert(packet.id);
    }

    static void detachDependencies(const RenderPacket& packet, GeometryDependents& geometryUsers,
                                   MaterialDependents& materialUsers, AssetDependents& assetUsers) {
        for (GeometryHandle handle : packet.geometryDependencies) {
            auto known = geometryUsers.find(handle);
            if (known == geometryUsers.end())
                continue;
            known->second.erase(packet.id);
            if (known->second.empty())
                geometryUsers.erase(known);
        }
        for (RenderMaterialHandle handle : packet.materialDependencies) {
            auto known = materialUsers.find(handle);
            if (known == materialUsers.end())
                continue;
            known->second.erase(packet.id);
            if (known->second.empty())
                materialUsers.erase(known);
        }
        for (RenderResourceKey key : packet.assetDependencies) {
            auto known = assetUsers.find(key);
            if (known == assetUsers.end())
                continue;
            known->second.erase(packet.id);
            if (known->second.empty())
                assetUsers.erase(known);
        }
    }

    static bool hasUnresolvedResource(const RenderPacket& packet) {
        return std::any_of(packet.drawables.begin(), packet.drawables.end(), [](const CachedRenderDrawable& drawable) {
            return drawable.geometryStatus == DrawableStatus::MissingGpuGeometry ||
                   drawable.baseTextureStatus == DrawableStatus::MissingGpuTexture;
        });
    }

    ResultVoid preflightPacket(const RenderPacket& packet, const RenderOptions& options,
                               const std::optional<math::Frustum3>& frustum, bool sceneFrustumCulling,
                               RenderCompilerStats& failureStats) const {
        if (!packet.sourceVisible)
            return {};
        if (sceneFrustumCulling && frustum && indexableBounds(packet.worldBounds) &&
            !frustum->intersects(paddedFrustumBounds(packet.worldBounds))) {
            return {};
        }
        for (const CachedRenderDrawable& drawable : packet.drawables) {
            const RenderPassKind pass = renderBucketPass(drawable.bucket);
            const RenderVisualMatch visual = renderVisualMatch(
                    drawable.bucket, packet.pickId, drawable.sourceDrawableIndex, packet.defaultSelected, options);
            const bool highlightWillEmit =
                    !renderBucketIsOverlay(drawable.bucket) &&
                    ((pass == RenderPassKind::Surface && visual.selected && renderSurfacesEnabled(options)) ||
                     (pass == RenderPassKind::Edge && (visual.selected || visual.hovered)));
            const bool baseWillEmit = bucketWillEmitBase(drawable.bucket, options);
            if (!baseWillEmit && !highlightWillEmit)
                continue;

            if (drawable.geometryStatus == DrawableStatus::MissingGpuGeometry) {
                ++failureStats.missingGpuGeometryCount;
                return std::unexpected(
                        Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU geometry."));
            }
            if (baseWillEmit && drawable.baseCommand.pipelineState &&
                drawable.materialStatus == DrawableStatus::Ready &&
                drawable.baseTextureStatus == DrawableStatus::MissingGpuTexture) {
                failureStats.missingGpuTextureCount += drawable.missingGpuTextureCount;
                return std::unexpected(
                        Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU texture."));
            }
        }
        return {};
    }

    ResultVoid preflightPackets(const PacketMap& candidates, const RenderOptions& options,
                                const std::optional<math::Frustum3>& frustum, bool sceneFrustumCulling,
                                RenderCompilerStats& failureStats) const {
        for (const auto& [_, packet] : candidates) {
            if (!hasUnresolvedResource(packet))
                continue;
            if (auto ready = preflightPacket(packet, options, frustum, sceneFrustumCulling, failureStats); !ready)
                return ready;
        }
        return {};
    }

    static void attachSpatial(const RenderPacket& packet, SpatialIndex& index, ObjectSet& uncullable,
                              size_t& visibleCount) {
        if (!packet.sourceVisible)
            return;
        ++visibleCount;
        if (!indexableBounds(packet.worldBounds) || !index.insert(packet.id, packet.worldBounds)) {
            // 索引拒绝任何输入时都退化为保守可见，绝不能因加速结构异常漏绘。
            uncullable.insert(packet.id);
        }
    }

    static void detachSpatial(const RenderPacket& packet, SpatialIndex& index, ObjectSet& uncullable,
                              size_t& visibleCount) {
        if (!packet.sourceVisible)
            return;
        if (visibleCount != 0)
            --visibleCount;
        index.remove(packet.id);
        uncullable.erase(packet.id);
    }

    ResultVoid rebuild(const RenderWorldSnapshot& current, const RenderOptions& options,
                       const std::optional<math::Frustum3>& frustum, bool sceneFrustumCulling,
                       RenderCompileContext& context) {
        ResolutionTransaction transaction;
        transaction.allowPersistent = false;
        transaction.geometries.reserve(current.geometries().size());
        transaction.materials.reserve(current.materials().size());

        PacketMap nextPackets;
        nextPackets.reserve(current.objects().size());
        std::vector<RenderObjectId> orderedPacketIds;
        orderedPacketIds.reserve(current.objects().size());
        for (const RenderObjectRecord& object : current.objects()) {
            RenderPacket compiled = compilePacket(current, object, context, transaction);
            nextPackets.emplace(object.id, std::move(compiled));
            orderedPacketIds.push_back(object.id);
        }

        RenderCompilerStats failureStats;
        if (auto ready = preflightPackets(nextPackets, options, frustum, sceneFrustumCulling, failureStats); !ready) {
            stats = failureStats;
            workloadStats.reset();
            invalidateAssemblyKey();
            return ready;
        }
        // MaterialCache 有外部可观察版本；只有所有硬资源预检成功后才允许注册。
        finalizePacketMaterials(nextPackets, orderedPacketIds, current.version().world, context, transaction);

        GeometryDependents nextGeometryDependents;
        MaterialDependents nextMaterialDependents;
        AssetDependents nextAssetDependents;
        GeometryResolutionsByAsset nextGeometryResolutionsByAsset;
        MaterialResolutionsByTexture nextMaterialResolutionsByTexture;
        SpatialIndex nextSpatialIndex;
        ObjectSet nextUncullable;
        ObjectSet nextUnresolved;
        size_t nextVisibleCount = 0;
        nextSpatialIndex.reserve(nextPackets.size());
        // PersistentRecordStore 以对象索引升序枚举；按同一稳定顺序构建 BVH，
        // 避免 unordered_map 的桶布局影响树形和性能诊断。
        for (RenderObjectId id : orderedPacketIds) {
            const RenderPacket& packet = nextPackets.at(id);
            attachDependencies(packet, nextGeometryDependents, nextMaterialDependents, nextAssetDependents);
            attachSpatial(packet, nextSpatialIndex, nextUncullable, nextVisibleCount);
            if (hasUnresolvedResource(packet))
                nextUnresolved.insert(id);
        }
        for (const auto& [handle, resolution] : transaction.geometries)
            attachGeometryResolution(handle, resolution, nextGeometryResolutionsByAsset);
        for (const auto& [handle, resolution] : transaction.materials)
            attachMaterialResolution(handle, resolution, nextMaterialResolutionsByTexture);

        packets = std::move(nextPackets);
        resolvedGeometries = std::move(transaction.geometries);
        resolvedMaterials = std::move(transaction.materials);
        geometryDependents = std::move(nextGeometryDependents);
        materialDependents = std::move(nextMaterialDependents);
        assetDependents = std::move(nextAssetDependents);
        geometryResolutionsByAsset = std::move(nextGeometryResolutionsByAsset);
        materialResolutionsByTexture = std::move(nextMaterialResolutionsByTexture);
        spatialIndex = std::move(nextSpatialIndex);
        uncullableObjects = std::move(nextUncullable);
        unresolvedPackets = std::move(nextUnresolved);
        sourceVisibleObjectCount = nextVisibleCount;
        snapshot = current;
        contextIdentity = ContextIdentity::capture(context);
        assetChangeCursor = context.assets.currentChangeCursor();
        visibilityCache.valid = false;
        advancePacketContentRevision();
        packetStats.recompiledPacketCount = packets.size();
        return {};
    }

    void markGeometryUsers(GeometryHandle handle, ObjectSet& dirty) const {
        if (const auto known = geometryDependents.find(handle); known != geometryDependents.end())
            dirty.insert(known->second.begin(), known->second.end());
    }

    void markMaterialUsers(RenderMaterialHandle handle, ObjectSet& dirty) const {
        if (const auto known = materialDependents.find(handle); known != materialDependents.end())
            dirty.insert(known->second.begin(), known->second.end());
    }

    void markAssetChange(const AssetGpuChange& change, ObjectSet& dirty, GeometrySet& invalidGeometries,
                         MaterialSet& invalidMaterials) const {
        // Packet 索引仅用于保守标脏；真正的 resolution 失效来自下方两个独立索引，
        // 因而最后一个 Packet 消失后的资源事件也不会被游标静默吞掉。
        if (const auto users = assetDependents.find(change.key); users != assetDependents.end())
            dirty.insert(users->second.begin(), users->second.end());

        switch (change.kind) {
        case AssetGpuChangeKind::GeometryUpserted:
        case AssetGpuChangeKind::GeometryRetired:
            if (const auto known = geometryResolutionsByAsset.find(change.key);
                known != geometryResolutionsByAsset.end()) {
                for (GeometryHandle handle : known->second) {
                    invalidGeometries.insert(handle);
                    markGeometryUsers(handle, dirty);
                }
            }
            break;
        case AssetGpuChangeKind::TextureUpserted:
        case AssetGpuChangeKind::TextureRetired:
            if (const auto known = materialResolutionsByTexture.find(change.key);
                known != materialResolutionsByTexture.end()) {
                for (RenderMaterialHandle handle : known->second) {
                    invalidMaterials.insert(handle);
                    markMaterialUsers(handle, dirty);
                }
            }
            break;
        case AssetGpuChangeKind::InvalidateAll: break;
        }
    }

    ResultVoid reconcile(const RenderWorldSnapshot& current, const RenderOptions& options,
                         const std::optional<math::Frustum3>& frustum, bool sceneFrustumCulling,
                         const AssetGpuChangeSet& assetChanges, RenderCompileContext& context) {
        ObjectSet dirty;
        ObjectSet removed;
        GeometrySet invalidGeometries;
        MaterialSet invalidMaterials;

        for (const AssetGpuChange& change : assetChanges.changes) {
            if (change.kind != AssetGpuChangeKind::InvalidateAll && change.key)
                markAssetChange(change, dirty, invalidGeometries, invalidMaterials);
        }

        current.forEachObjectDifference(
                *snapshot, [&](uint32_t, const RenderObjectRecord* previous, const RenderObjectRecord* next) {
                    // 同一 ID 的普通更新是替换，不是删除；只有记录消失或槽位 generation
                    // 改变时才退役旧 Packet。否则会把刚加入 dirty 的更新错误抹掉。
                    if (previous && (!next || previous->id != next->id))
                        removed.insert(previous->id);
                    if (next)
                        dirty.insert(next->id);
                });
        current.forEachGeometryDifference(
                *snapshot, [&](uint32_t, const RenderGeometryRecord* previous, const RenderGeometryRecord* next) {
                    if (previous) {
                        invalidGeometries.insert(previous->handle);
                        markGeometryUsers(previous->handle, dirty);
                    }
                    if (next) {
                        invalidGeometries.insert(next->handle);
                        markGeometryUsers(next->handle, dirty);
                    }
                });
        current.forEachMaterialDifference(
                *snapshot, [&](uint32_t, const RenderMaterialRecord* previous, const RenderMaterialRecord* next) {
                    if (previous) {
                        invalidMaterials.insert(previous->handle);
                        markMaterialUsers(previous->handle, dirty);
                    }
                    if (next) {
                        invalidMaterials.insert(next->handle);
                        markMaterialUsers(next->handle, dirty);
                    }
                });
        for (RenderObjectId id : removed)
            dirty.erase(id);

        std::vector<RenderObjectId> orderedDirty(dirty.begin(), dirty.end());
        std::sort(orderedDirty.begin(), orderedDirty.end(), objectIdLess);

        ResolutionTransaction transaction;
        transaction.invalidGeometries = &invalidGeometries;
        transaction.invalidMaterials = &invalidMaterials;
        transaction.geometries.reserve(invalidGeometries.size() + orderedDirty.size());
        transaction.materials.reserve(invalidMaterials.size() + orderedDirty.size());

        PacketMap replacements;
        replacements.reserve(orderedDirty.size());
        for (RenderObjectId id : orderedDirty) {
            const RenderObjectRecord* object = current.object(id);
            if (!object)
                continue;
            RenderPacket compiled = compilePacket(current, *object, context, transaction);
            replacements.emplace(id, std::move(compiled));
        }

        ObjectSet replacedOrRemoved = removed;
        replacedOrRemoved.insert(dirty.begin(), dirty.end());

        RenderCompilerStats failureStats;
        if (auto ready = preflightPackets(replacements, options, frustum, sceneFrustumCulling, failureStats); !ready) {
            stats = failureStats;
            workloadStats.reset();
            invalidateAssemblyKey();
            return ready;
        }
        for (RenderObjectId id : unresolvedPackets) {
            if (replacedOrRemoved.contains(id))
                continue;
            const auto known = packets.find(id);
            if (known != packets.end()) {
                if (auto ready = preflightPacket(known->second, options, frustum, sceneFrustumCulling, failureStats);
                    !ready) {
                    stats = failureStats;
                    workloadStats.reset();
                    invalidateAssemblyKey();
                    return ready;
                }
            }
        }
        finalizePacketMaterials(replacements, orderedDirty, current.version().world, context, transaction);

        // 所有本帧会发射的资源均已验证；以下步骤才发布新缓存状态。
        std::vector<RenderObjectId> orderedRetire(replacedOrRemoved.begin(), replacedOrRemoved.end());
        std::sort(orderedRetire.begin(), orderedRetire.end(), objectIdLess);
        for (RenderObjectId id : orderedRetire) {
            auto known = packets.find(id);
            if (known == packets.end())
                continue;
            detachDependencies(known->second, geometryDependents, materialDependents, assetDependents);
            detachSpatial(known->second, spatialIndex, uncullableObjects, sourceVisibleObjectCount);
            unresolvedPackets.erase(id);
            packets.erase(known);
        }
        for (GeometryHandle handle : invalidGeometries) {
            const auto known = resolvedGeometries.find(handle);
            if (known == resolvedGeometries.end())
                continue;
            detachGeometryResolution(handle, known->second, geometryResolutionsByAsset);
            resolvedGeometries.erase(known);
        }
        for (RenderMaterialHandle handle : invalidMaterials) {
            const auto known = resolvedMaterials.find(handle);
            if (known == resolvedMaterials.end())
                continue;
            detachMaterialResolution(handle, known->second, materialResolutionsByTexture);
            resolvedMaterials.erase(known);
        }
        for (auto& [handle, resolved] : transaction.geometries) {
            auto stored = resolvedGeometries.insert_or_assign(handle, std::move(resolved)).first;
            attachGeometryResolution(handle, stored->second, geometryResolutionsByAsset);
        }
        for (auto& [handle, resolved] : transaction.materials) {
            auto stored = resolvedMaterials.insert_or_assign(handle, std::move(resolved)).first;
            attachMaterialResolution(handle, stored->second, materialResolutionsByTexture);
        }
        for (RenderObjectId id : orderedDirty) {
            auto replacement = replacements.find(id);
            if (replacement == replacements.end())
                continue;
            RenderPacket& packet = replacement->second;
            attachDependencies(packet, geometryDependents, materialDependents, assetDependents);
            attachSpatial(packet, spatialIndex, uncullableObjects, sourceVisibleObjectCount);
            if (hasUnresolvedResource(packet))
                unresolvedPackets.insert(id);
            packets.emplace(id, std::move(packet));
        }

        snapshot = current;
        contextIdentity = ContextIdentity::capture(context);
        assetChangeCursor = assetChanges.cursorAfterApply();
        if (!replacedOrRemoved.empty()) {
            visibilityCache.valid = false;
            advancePacketContentRevision();
        }
        packetStats.recompiledPacketCount = replacements.size();
        packetStats.reusedPacketCount =
                packets.size() >= replacements.size() ? packets.size() - replacements.size() : 0;
        packetStats.cacheHit = replacements.empty() && removed.empty();
        return {};
    }

    std::vector<RenderObjectId> allVisiblePacketIds() const {
        std::vector<RenderObjectId> ids;
        ids.reserve(sourceVisibleObjectCount);
        for (const auto& [id, packet] : packets) {
            if (packet.sourceVisible)
                ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), objectIdLess);
        return ids;
    }

    std::vector<RenderObjectId> visiblePacketIds(const RenderViewDesc* view, bool sceneFrustumCulling) {
        packetStats.sourceVisibleObjectCount = sourceVisibleObjectCount;
        packetStats.uncullableObjectCount = uncullableObjects.size();
        if (!sceneFrustumCulling) {
            auto ids = allVisiblePacketIds();
            packetStats.frustumVisibleObjectCount = ids.size();
            return ids;
        }

        if (view && visibilityCache.valid && visibilityCache.width == view->width &&
            visibilityCache.height == view->height && matricesEqual(visibilityCache.view, view->viewMatrix) &&
            matricesEqual(visibilityCache.projection, view->projectionMatrix)) {
            packetStats.frustumFailOpen = visibilityCache.failOpen;
            packetStats.bvhNodeBoundsTestCount = visibilityCache.queryStats.nodeBoundsTestCount;
            packetStats.bvhLeafBoundsTestCount = visibilityCache.queryStats.leafBoundsTestCount;
            packetStats.frustumVisibleObjectCount = visibilityCache.ids.size();
            packetStats.culledObjectCount = sourceVisibleObjectCount - visibilityCache.ids.size();
            return visibilityCache.ids;
        }

        std::vector<RenderObjectId> ids;
        SpatialIndex::QueryStats queryStats;
        bool failOpen = view == nullptr;
        std::optional<math::Frustum3> frustum;
        if (view)
            frustum = math::Frustum3::tryFromViewProjection(view->projectionMatrix * view->viewMatrix);
        if (!frustum) {
            failOpen = true;
            ids = allVisiblePacketIds();
        } else {
            ids.reserve(spatialIndex.size() + uncullableObjects.size());
            const double padding = math::defaultTolerance().lengthEps;
            spatialIndex.queryFrustum(
                    *frustum, padding,
                    [&](RenderObjectId id) {
                        ids.push_back(id);
                        return true;
                    },
                    &queryStats);
            ids.insert(ids.end(), uncullableObjects.begin(), uncullableObjects.end());
            std::sort(ids.begin(), ids.end(), objectIdLess);
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }

        packetStats.frustumFailOpen = failOpen;
        packetStats.bvhNodeBoundsTestCount = queryStats.nodeBoundsTestCount;
        packetStats.bvhLeafBoundsTestCount = queryStats.leafBoundsTestCount;
        packetStats.frustumVisibleObjectCount = ids.size();
        packetStats.culledObjectCount = sourceVisibleObjectCount - ids.size();

        if (view) {
            visibilityCache.valid = true;
            visibilityCache.view = view->viewMatrix;
            visibilityCache.projection = view->projectionMatrix;
            visibilityCache.width = view->width;
            visibilityCache.height = view->height;
            visibilityCache.failOpen = failOpen;
            visibilityCache.queryStats = queryStats;
            visibilityCache.ids = ids;
        }
        return ids;
    }

    ResultVoid appendCommand(const CachedRenderDrawable& drawable, bool highlight, bool selected, bool hovered,
                             std::vector<MeshDrawCommand>& destination, RenderCompilerStats& frameStats,
                             size_t& acceptedCount) {
        if (drawable.geometryStatus != DrawableStatus::Ready) {
            recordDrawableFailure(drawable.geometryStatus, frameStats);
            if (drawable.geometryStatus == DrawableStatus::MissingGpuGeometry) {
                return std::unexpected(
                        Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU geometry."));
            }
            return {};
        }
        PipelineState* pipeline = highlight ? drawable.highlightPipeline : drawable.baseCommand.pipelineState;
        if (!pipeline) {
            ++frameStats.missingPipelineCount;
            return {};
        }
        if (drawable.materialStatus != DrawableStatus::Ready) {
            recordDrawableFailure(drawable.materialStatus, frameStats);
            return {};
        }
        if (!highlight && drawable.baseTextureStatus == DrawableStatus::MissingGpuTexture) {
            frameStats.missingGpuTextureCount += drawable.missingGpuTextureCount;
            return std::unexpected(
                    Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU texture."));
        }
        MeshDrawCommand command = drawable.baseCommand;
        command.pipelineState = pipeline;
        command.selected = selected;
        command.hovered = hovered;
        command.batchInstancingEligible = !highlight && !renderBucketIsOverlay(drawable.bucket) && !command.translucent;
        destination.push_back(std::move(command));
        ++acceptedCount;
        return {};
    }

    ResultVoid assemble(const RenderOptions& options, const std::vector<RenderObjectId>& visibleIds) {
        if (assembledOptions && assembledPacketContentRevision == packetContentRevision &&
            assembledVisibleIds == visibleIds && commandOptionsEqual(*assembledOptions, options)) {
            packetStats.assemblyCacheHit = true;
            packetStats.assembledCommandCount = surfaceCommands.size() + edgeCommands.size() +
                                                highlightSurfaceCommands.size() + highlightEdgeCommands.size();
            return {};
        }

        scratchSurfaceCommands.clear();
        scratchEdgeCommands.clear();
        scratchHighlightSurfaceCommands.clear();
        scratchHighlightEdgeCommands.clear();
        RenderCompilerStats nextStats;
        RenderWorkloadStats nextWorkloadStats;

        const auto abortAssembly = [&](ResultVoid error) -> ResultVoid {
            scratchSurfaceCommands.clear();
            scratchEdgeCommands.clear();
            scratchHighlightSurfaceCommands.clear();
            scratchHighlightEdgeCommands.clear();
            stats = nextStats;
            workloadStats = nextWorkloadStats;
            invalidateAssemblyKey();
            return error;
        };

        nextWorkloadStats.visibleObjectCount = visibleIds.size();
        for (RenderObjectId id : visibleIds) {
            const auto known = packets.find(id);
            if (known == packets.end())
                continue;
            const RenderPacket& packet = known->second;
            for (const CachedRenderDrawable& drawable : packet.drawables) {
                ++nextWorkloadStats.drawableCount;
                const RenderPassKind pass = renderBucketPass(drawable.bucket);
                const RenderVisualMatch visual = renderVisualMatch(
                        drawable.bucket, packet.pickId, drawable.sourceDrawableIndex, packet.defaultSelected, options);

                if (!renderBucketIsOverlay(drawable.bucket)) {
                    if (visual.selected && pass == RenderPassKind::Surface && renderSurfacesEnabled(options)) {
                        ++nextWorkloadStats.highlightSurfaceItemCount;
                        ++nextStats.highlightSurfaceWorkItemCount;
                        auto appended = appendCommand(drawable, true, visual.selected, visual.hovered,
                                                      scratchHighlightSurfaceCommands, nextStats,
                                                      nextStats.acceptedHighlightSurfaceCommandCount);
                        if (!appended)
                            return abortAssembly(appended);
                    }
                    if ((visual.selected || visual.hovered) && pass == RenderPassKind::Edge) {
                        ++nextWorkloadStats.highlightEdgeItemCount;
                        ++nextStats.highlightEdgeWorkItemCount;
                        auto appended = appendCommand(drawable, true, visual.selected, visual.hovered,
                                                      scratchHighlightEdgeCommands, nextStats,
                                                      nextStats.acceptedHighlightEdgeCommandCount);
                        if (!appended)
                            return abortAssembly(appended);
                    }
                }

                if (!bucketEnabled(drawable.bucket, options, nextWorkloadStats))
                    continue;
                switch (pass) {
                case RenderPassKind::Surface:
                    ++nextWorkloadStats.surfaceItemCount;
                    ++nextStats.surfaceWorkItemCount;
                    if (auto appended = appendCommand(drawable, false, false, false, scratchSurfaceCommands, nextStats,
                                                      nextStats.acceptedSurfaceCommandCount);
                        !appended)
                        return abortAssembly(appended);
                    break;
                case RenderPassKind::Edge:
                    ++nextWorkloadStats.edgeItemCount;
                    ++nextStats.edgeWorkItemCount;
                    if (auto appended = appendCommand(drawable, false, false, false, scratchEdgeCommands, nextStats,
                                                      nextStats.acceptedEdgeCommandCount);
                        !appended)
                        return abortAssembly(appended);
                    break;
                case RenderPassKind::None: break;
                }
            }
        }

        sortDrawCommands(scratchSurfaceCommands);
        sortDrawCommands(scratchEdgeCommands);
        // Highlight pass 使用 alpha blend，覆盖顺序属于视觉语义，保持对象/Drawable 稳定顺序。
        for (auto& command : scratchHighlightSurfaceCommands)
            updateSortKey(command);
        for (auto& command : scratchHighlightEdgeCommands)
            updateSortKey(command);
        surfaceCommands.swap(scratchSurfaceCommands);
        edgeCommands.swap(scratchEdgeCommands);
        highlightSurfaceCommands.swap(scratchHighlightSurfaceCommands);
        highlightEdgeCommands.swap(scratchHighlightEdgeCommands);
        scratchSurfaceCommands.clear();
        scratchEdgeCommands.clear();
        scratchHighlightSurfaceCommands.clear();
        scratchHighlightEdgeCommands.clear();
        stats = nextStats;
        workloadStats = nextWorkloadStats;
        assembledPacketContentRevision = packetContentRevision;
        assembledOptions = options;
        assembledVisibleIds = visibleIds;
        advanceCommandRevision();
        packetStats.assembledCommandCount = surfaceCommands.size() + edgeCommands.size() +
                                            highlightSurfaceCommands.size() + highlightEdgeCommands.size();
        return {};
    }
};

RenderCompiler::RenderCompiler() : impl_(std::make_unique<Impl>()) {
}

RenderCompiler::~RenderCompiler() = default;

ResultVoid RenderCompiler::compile(const RenderWorldSnapshot& snapshot, const RenderOptions& options,
                                   RenderCompileContext& context, const RenderViewDesc* view,
                                   bool sceneFrustumCulling) {
    impl_->packetStats.reset();

    AssetGpuChangeSet assetChanges;
    bool assetRequiresFullRebuild = false;
    if (impl_->snapshot && impl_->contextIdentity.assets == &context.assets) {
        assetChanges = context.assets.readChanges(impl_->assetChangeCursor);
        assetRequiresFullRebuild =
                assetChanges.requiresFullResync() ||
                std::any_of(assetChanges.changes.begin(), assetChanges.changes.end(), [](const AssetGpuChange& change) {
                    return change.kind == AssetGpuChangeKind::InvalidateAll;
                });
    }
    const bool fullRebuild = !impl_->snapshot || impl_->snapshot->version().world != snapshot.version().world ||
                             !impl_->contextIdentity.matches(context) || assetRequiresFullRebuild;
    std::optional<math::Frustum3> preflightFrustum;
    if (sceneFrustumCulling && view) {
        preflightFrustum = math::Frustum3::tryFromViewProjection(view->projectionMatrix * view->viewMatrix);
    }
    impl_->packetStats.fullRebuild = fullRebuild;
    ResultVoid synchronized =
            fullRebuild
                    ? impl_->rebuild(snapshot, options, preflightFrustum, sceneFrustumCulling, context)
                    : impl_->reconcile(snapshot, options, preflightFrustum, sceneFrustumCulling, assetChanges, context);
    if (!synchronized) {
        impl_->workloadStats.reset();
        return synchronized;
    }

    // Material 注册可能在同步期间推进版本，成功发布时必须记录最终版本。
    impl_->contextIdentity = ContextIdentity::capture(context);
    if (fullRebuild) {
        impl_->packetStats.reusedPacketCount = 0;
        impl_->packetStats.cacheHit = false;
    }
    const std::vector<RenderObjectId> visibleIds = impl_->visiblePacketIds(view, sceneFrustumCulling);
    return impl_->assemble(options, visibleIds);
}

void RenderCompiler::clear() {
    if (!impl_->snapshot && impl_->surfaceCommands.empty() && impl_->edgeCommands.empty() &&
        impl_->highlightSurfaceCommands.empty() && impl_->highlightEdgeCommands.empty()) {
        return;
    }
    const uint64_t previousRevision = impl_->commandRevision;
    impl_ = std::make_unique<Impl>();
    impl_->commandRevision = previousRevision;
    impl_->advanceCommandRevision();
}

std::span<const MeshDrawCommand> RenderCompiler::surfaceCommands() const {
    return impl_->surfaceCommands;
}

std::span<const MeshDrawCommand> RenderCompiler::edgeCommands() const {
    return impl_->edgeCommands;
}

std::span<const MeshDrawCommand> RenderCompiler::highlightSurfaceCommands() const {
    return impl_->highlightSurfaceCommands;
}

std::span<const MeshDrawCommand> RenderCompiler::highlightEdgeCommands() const {
    return impl_->highlightEdgeCommands;
}

const RenderCompilerStats& RenderCompiler::lastStats() const {
    return impl_->stats;
}

const RenderWorkloadStats& RenderCompiler::lastWorkloadStats() const {
    return impl_->workloadStats;
}

const RenderPacketCacheStats& RenderCompiler::lastPacketCacheStats() const {
    return impl_->packetStats;
}

uint64_t RenderCompiler::commandRevision() const {
    return impl_->commandRevision;
}

}  // namespace mulan::engine
