#include "render_compiler.h"

#include "../asset_gpu_registry.h"
#include "../frontend/render_contract.h"
#include "../material/material_cache.h"
#include "../render_geometry.h"
#include "surface_pipeline_provider.h"
#include "../../rhi/engine_error_code.h"

#include <mulan/core/profiling/profile.h>
#include <mulan/math/spatial/dynamic_bvh.h>

#include <algorithm>
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
using SpatialIndex = math::DynamicBVH<RenderObjectId, ObjectIdHash>;

template <typename T>
int compareValue(const T& lhs, const T& rhs) noexcept {
    if (std::less<T>{}(lhs, rhs))
        return -1;
    if (std::less<T>{}(rhs, lhs))
        return 1;
    return 0;
}

bool opaqueCommandLess(const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) noexcept {
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
    if (const int order = compareValue(lhs.ambientTex, rhs.ambientTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.specularTex, rhs.specularTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.shininessTex, rhs.shininessTex); order != 0)
        return order < 0;
    if (const int order = compareValue(lhs.opacityTex, rhs.opacityTex); order != 0)
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

void sortDrawCommands(std::vector<MeshDrawCommand>& commands, const math::Mat4* viewMatrix = nullptr) {
    const auto opaqueEnd = std::stable_partition(commands.begin(), commands.end(),
                                                 [](const MeshDrawCommand& command) { return !command.translucent; });
    std::sort(commands.begin(), opaqueEnd, opaqueCommandLess);
    if (viewMatrix) {
        std::stable_sort(opaqueEnd, commands.end(), [&](const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) {
            const double lhsViewZ = lhs.sortCenter.transformedBy(*viewMatrix).z;
            const double rhsViewZ = rhs.sortCenter.transformedBy(*viewMatrix).z;
            const bool lhsFinite = std::isfinite(lhsViewZ);
            const bool rhsFinite = std::isfinite(rhsViewZ);
            if (lhsFinite != rhsFinite)
                return lhsFinite;
            if (lhsFinite && lhsViewZ != rhsViewZ)
                return lhsViewZ < rhsViewZ;
            return lhs.pickId < rhs.pickId;
        });
    }
}

bool objectIdLess(RenderObjectId lhs, RenderObjectId rhs) noexcept {
    return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
}

bool matricesEqual(const math::Mat4& lhs, const math::Mat4& rhs) noexcept {
    for (int index = 0; index < 16; ++index) {
        if (lhs.data()[index] != rhs.data()[index])
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
    if (lhs.displayMode != rhs.displayMode || lhs.hoveredPickId != rhs.hoveredPickId ||
        lhs.showSurfaces != rhs.showSurfaces || lhs.showEdges != rhs.showEdges ||
        lhs.showOverlays != rhs.showOverlays || lhs.selectionVisuals.active() != rhs.selectionVisuals.active()) {
        return false;
    }
    const auto lhsTargets = lhs.selectionVisuals.targets();
    const auto rhsTargets = rhs.selectionVisuals.targets();
    return lhsTargets.size() == rhsTargets.size() &&
           std::equal(lhsTargets.begin(), lhsTargets.end(), rhsTargets.begin(), selectionTargetEqual);
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

bool indexableBounds(const math::AABB3& bounds) {
    return SpatialIndex::isValidBounds(bounds) && !bounds.isEmpty();
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
    DrawableStatus geometryStatus = DrawableStatus::Ready;
    DrawableStatus materialStatus = DrawableStatus::Ready;
    DrawableStatus textureStatus = DrawableStatus::Ready;
    size_t missingTextureCount = 0;
    MeshDrawCommand baseCommand;
    PipelineState* highlightPipeline = nullptr;
};

struct RenderPacket {
    RenderObjectId id;
    PickId pickId;
    math::AABB3 worldBounds;
    bool visible = false;
    bool selected = false;
    std::vector<CachedRenderDrawable> drawables;
};

struct ContextIdentity {
    const AssetGpuRegistry* assets = nullptr;
    const MaterialCache* materials = nullptr;
    uint64_t assetRevision = 0;
    uint64_t materialLayoutRevision = 0;
    const SurfacePipelineProvider* surfacePipelines = nullptr;
    PipelineState* edgePipeline = nullptr;
    PipelineState* highlightSurfacePipeline = nullptr;
    PipelineState* highlightSurfaceTangentPipeline = nullptr;
    PipelineState* highlightEdgePipeline = nullptr;

    bool matches(const RenderCompileContext& context) const noexcept {
        return assets == &context.assets && materials == &context.materials &&
               assetRevision == context.assets.revision() &&
               materialLayoutRevision == context.materials.layoutRevision() &&
               surfacePipelines == context.surfacePipelines && edgePipeline == context.edgePipeline &&
               highlightSurfacePipeline == context.highlightSurfacePipeline &&
               highlightSurfaceTangentPipeline == context.highlightSurfaceTangentPipeline &&
               highlightEdgePipeline == context.highlightEdgePipeline;
    }

    static ContextIdentity capture(const RenderCompileContext& context) noexcept {
        return { .assets = &context.assets,
                 .materials = &context.materials,
                 .assetRevision = context.assets.revision(),
                 .materialLayoutRevision = context.materials.layoutRevision(),
                 .surfacePipelines = context.surfacePipelines,
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

std::optional<uint32_t> resolveMaterialIndex(const RenderWorldSnapshot& snapshot, RenderMaterialHandle handle,
                                             uint64_t worldId, MaterialCache& cache) {
    const RenderMaterialRecord* record = snapshot.material(handle);
    if (!record || record->desc.resourceKey == defaultRenderMaterialResourceKey())
        return uint32_t{ 0 };

    const std::string name = record->desc.resourceKey
                                     ? "render-material:" + std::to_string(record->desc.resourceKey.domain.value) +
                                               ":" + std::to_string(record->desc.resourceKey.source) + ":" +
                                               std::to_string(record->desc.resourceKey.subresource) + ":" +
                                               std::to_string(static_cast<uint8_t>(record->desc.resourceKey.kind))
                                     : "render-material-world:" + std::to_string(worldId) + ":" +
                                               std::to_string(handle.generation) + ":" + std::to_string(handle.index);
    const MaterialHandle registered = cache.registerMaterial(name, record->desc.material);
    if (registered == kInvalidMaterialHandle || registered > std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    return static_cast<uint32_t>(registered);
}

Texture* resolveTexture(AssetGpuRegistry& assets, const RenderTextureDesc& desc, size_t& missingCount) {
    if (!desc.resourceKey || !desc.image || !desc.image->valid())
        return nullptr;
    const TextureLoadOptions options{ .generateMips = desc.generateMips, .sRGB = desc.srgb };
    Texture* texture = assets.findTexture(desc.resourceKey, options);
    if (!texture)
        ++missingCount;
    return texture;
}

RenderPacket buildPacket(const RenderWorldSnapshot& snapshot, const RenderObjectRecord& object,
                         RenderCompileContext& context) {
    RenderPacket packet;
    packet.id = object.id;
    packet.pickId = object.desc.pickId;
    packet.worldBounds = object.desc.worldBounds;
    packet.visible = object.desc.visible;
    packet.selected = object.desc.selected;
    if (!packet.visible)
        return packet;

    packet.drawables.reserve(object.desc.drawables.size());
    for (const RenderObjectDrawable& drawable : object.desc.drawables) {
        CachedRenderDrawable cached;
        cached.bucket = drawable.bucket;
        cached.sourceDrawableIndex = drawable.sourceDrawableIndex;
        if (renderBucketPass(drawable.bucket) == RenderPassKind::None) {
            packet.drawables.push_back(std::move(cached));
            continue;
        }

        const RenderGeometryRecord* geometryRecord = snapshot.geometry(drawable.geometry);
        if (!geometryRecord) {
            cached.geometryStatus = DrawableStatus::MissingGeometryRecord;
            packet.drawables.push_back(std::move(cached));
            continue;
        }
        if (geometryRecord->desc.empty) {
            cached.geometryStatus = DrawableStatus::EmptyGeometry;
            packet.drawables.push_back(std::move(cached));
            continue;
        }
        const GpuGeometry* geometry = context.assets.findGeometry(geometryRecord->desc.resourceKey);
        if (!geometry) {
            cached.geometryStatus = DrawableStatus::MissingGpuGeometry;
            packet.drawables.push_back(std::move(cached));
            continue;
        }
        if (!renderGpuGeometryMatchesBucket(drawable.bucket, geometryRecord->desc, *geometry)) {
            cached.geometryStatus = DrawableStatus::RejectedContract;
            packet.drawables.push_back(std::move(cached));
            continue;
        }

        const bool tangentLayout = geometry->layout.has(graphics::VertexSemantic::Tangent);
        const RenderMaterialRecord* materialRecord = snapshot.material(drawable.material);
        static const Material fallbackMaterial = Material::defaultSurface();
        const Material& materialSemantics = materialRecord ? materialRecord->desc.material : fallbackMaterial;
        const auto shadingModel = materialSemantics.shadingModel;
        const auto alphaMode = materialSemantics.alphaMode;
        const bool doubleSided = materialSemantics.doubleSided;
        const double transformDeterminant = math::Mat3(object.desc.worldTransform).determinant();
        const bool singularTransform = !std::isfinite(transformDeterminant) || std::abs(transformDeterminant) <= 1e-12;
        switch (renderBucketPass(drawable.bucket)) {
        case RenderPassKind::Surface:
            cached.baseCommand.surfaceFamily = surfacePipelineFamily(shadingModel, tangentLayout);
            switch (cached.baseCommand.surfaceFamily) {
            case SurfacePipelineFamily::Unlit:
            case SurfacePipelineFamily::UnlitTangent:
                cached.baseCommand.materialBindings = MaterialBindingProfile::Unlit;
                break;
            case SurfacePipelineFamily::Legacy:
            case SurfacePipelineFamily::LegacyTangent:
                cached.baseCommand.materialBindings = MaterialBindingProfile::Legacy;
                break;
            case SurfacePipelineFamily::PBR:
            case SurfacePipelineFamily::PBRTangent:
                cached.baseCommand.materialBindings = MaterialBindingProfile::PBR;
                break;
            }
            cached.baseCommand.pipelineState =
                    context.surfacePipelines ? context.surfacePipelines->acquireSurfacePipeline(SurfacePipelineRequest{
                                                       .family = cached.baseCommand.surfaceFamily,
                                                       .alphaMode = alphaMode,
                                                       .doubleSided = doubleSided || singularTransform,
                                                       .reverseWinding = transformDeterminant < 0.0,
                                               })
                                             : nullptr;
            cached.baseCommand.translucent = alphaMode == graphics::AlphaMode::Blend;
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

        const std::optional<uint32_t> material =
                resolveMaterialIndex(snapshot, drawable.material, snapshot.version().world, context.materials);
        if (!material) {
            cached.materialStatus = DrawableStatus::MaterialRegistrationFailure;
        } else {
            cached.baseCommand.materialIndex = *material;
        }

        MeshDrawCommand& command = cached.baseCommand;
        command.vertexBuffer = geometry->vertexBuffer.get();
        command.indexBuffer = geometry->indexBuffer.get();
        command.indexCount = geometry->indexCount;
        command.indexType = geometry->indexType;
        command.vertexCount = geometry->vertexCount;
        command.instanceCount = 1;
        command.topology = geometryRecord->desc.topology;
        command.worldTransform = object.desc.worldTransform;
        command.sortCenter = object.desc.worldBounds.center();
        command.pickId = object.desc.pickId.valueOr(0);
        command.isWire = renderBucketPass(drawable.bucket) == RenderPassKind::Edge;

        if (renderBucketPass(drawable.bucket) == RenderPassKind::Surface) {
            if (materialRecord) {
                command.albedoTex = resolveTexture(context.assets, materialRecord->desc.baseColorTexture,
                                                   cached.missingTextureCount);
                command.normalTex =
                        resolveTexture(context.assets, materialRecord->desc.normalTexture, cached.missingTextureCount);
                command.mrTex = resolveTexture(context.assets, materialRecord->desc.metallicRoughnessTexture,
                                               cached.missingTextureCount);
                command.emissiveTex = resolveTexture(context.assets, materialRecord->desc.emissiveTexture,
                                                     cached.missingTextureCount);
                command.aoTex = resolveTexture(context.assets, materialRecord->desc.ambientOcclusionTexture,
                                               cached.missingTextureCount);
                command.ambientTex =
                        resolveTexture(context.assets, materialRecord->desc.ambientTexture, cached.missingTextureCount);
                command.specularTex = resolveTexture(context.assets, materialRecord->desc.specularTexture,
                                                     cached.missingTextureCount);
                command.shininessTex = resolveTexture(context.assets, materialRecord->desc.shininessTexture,
                                                      cached.missingTextureCount);
                command.opacityTex =
                        resolveTexture(context.assets, materialRecord->desc.opacityTexture, cached.missingTextureCount);
            }
            if (cached.missingTextureCount != 0)
                cached.textureStatus = DrawableStatus::MissingGpuTexture;
        }
        packet.drawables.push_back(std::move(cached));
    }
    return packet;
}

}  // namespace

struct RenderCompiler::Impl {
    using PacketMap = std::unordered_map<RenderObjectId, RenderPacket, ObjectIdHash>;
    using ObjectSet = std::unordered_set<RenderObjectId, ObjectIdHash>;

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

    std::optional<RenderWorldVersion> worldVersion;
    ContextIdentity contextIdentity;
    PacketMap packets;
    SpatialIndex spatialIndex;
    ObjectSet uncullableObjects;
    size_t sourceVisibleObjectCount = 0;
    VisibilityCache visibilityCache;
    uint64_t packetRevision = 1;
    uint64_t assembledPacketRevision = 0;
    std::optional<RenderOptions> assembledOptions;
    math::Mat4 assembledViewMatrix{ 1.0 };
    bool assembledHasTranslucent = false;
    std::vector<RenderObjectId> assembledVisibleIds;

    std::vector<MeshDrawCommand> surfaceCommands;
    std::vector<MeshDrawCommand> edgeCommands;
    std::vector<MeshDrawCommand> highlightSurfaceCommands;
    std::vector<MeshDrawCommand> highlightEdgeCommands;
    RenderCompilerStats stats;
    RenderWorkloadStats workloadStats;
    RenderPacketCacheStats packetStats;
    uint64_t commandRevision = 1;

    void advancePacketRevision() noexcept {
        if (++packetRevision == 0)
            ++packetRevision;
    }

    void advanceCommandRevision() noexcept {
        if (++commandRevision == 0)
            ++commandRevision;
    }

    void invalidateAssembly() {
        assembledPacketRevision = 0;
        assembledOptions.reset();
        assembledVisibleIds.clear();
    }

    void rebuild(const RenderWorldSnapshot& snapshot, RenderCompileContext& context) {
        MULAN_PROFILE_ZONE();

        packets.clear();
        spatialIndex.clear();
        uncullableObjects.clear();
        sourceVisibleObjectCount = 0;
        spatialIndex.reserve(snapshot.objects().size());
        packets.reserve(snapshot.objects().size());

        for (const RenderObjectRecord& object : snapshot.objects()) {
            RenderPacket packet = buildPacket(snapshot, object, context);
            if (packet.visible) {
                ++sourceVisibleObjectCount;
                if (!indexableBounds(packet.worldBounds) || !spatialIndex.insert(packet.id, packet.worldBounds))
                    uncullableObjects.insert(packet.id);
            }
            packets.emplace(packet.id, std::move(packet));
        }
        worldVersion = snapshot.version();
        contextIdentity = ContextIdentity::capture(context);
        visibilityCache.valid = false;
        advancePacketRevision();
        invalidateAssembly();
    }

    std::vector<RenderObjectId> allVisiblePacketIds() const {
        std::vector<RenderObjectId> ids;
        ids.reserve(sourceVisibleObjectCount);
        for (const auto& [id, packet] : packets) {
            if (packet.visible)
                ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), objectIdLess);
        return ids;
    }

    std::vector<RenderObjectId> visiblePacketIds(const RenderViewDesc* view, bool sceneFrustumCulling) {
        MULAN_PROFILE_ZONE();

        packetStats.sourceVisibleObjectCount = sourceVisibleObjectCount;
        packetStats.uncullableObjectCount = uncullableObjects.size();
        if (!sceneFrustumCulling) {
            std::vector<RenderObjectId> ids = allVisiblePacketIds();
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
            spatialIndex.queryFrustum(
                    *frustum, math::defaultTolerance().lengthEps,
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
                             std::vector<MeshDrawCommand>& destination, size_t& acceptedCount) {
        if (drawable.geometryStatus != DrawableStatus::Ready) {
            recordDrawableFailure(drawable.geometryStatus, stats);
            if (drawable.geometryStatus == DrawableStatus::MissingGpuGeometry) {
                return std::unexpected(
                        Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU geometry."));
            }
            return {};
        }
        PipelineState* pipeline = highlight ? drawable.highlightPipeline : drawable.baseCommand.pipelineState;
        if (!pipeline) {
            ++stats.missingPipelineCount;
            return {};
        }
        if (drawable.materialStatus != DrawableStatus::Ready) {
            recordDrawableFailure(drawable.materialStatus, stats);
            return {};
        }
        if (!highlight && drawable.textureStatus == DrawableStatus::MissingGpuTexture) {
            stats.missingGpuTextureCount += drawable.missingTextureCount;
            return std::unexpected(
                    Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU texture."));
        }

        MeshDrawCommand command = drawable.baseCommand;
        command.pipelineState = pipeline;
        if (highlight)
            command.materialBindings = MaterialBindingProfile::None;
        command.selected = selected;
        command.hovered = hovered;
        command.batchInstancingEligible = !highlight && !renderBucketIsOverlay(drawable.bucket) && !command.translucent;
        destination.push_back(std::move(command));
        ++acceptedCount;
        return {};
    }

    ResultVoid assemble(const RenderOptions& options, const std::vector<RenderObjectId>& visibleIds,
                        const RenderViewDesc* view) {
        MULAN_PROFILE_ZONE();

        const bool transparentOrderStable =
                !assembledHasTranslucent || !view || matricesEqual(assembledViewMatrix, view->viewMatrix);
        if (assembledOptions && assembledPacketRevision == packetRevision && assembledVisibleIds == visibleIds &&
            commandOptionsEqual(*assembledOptions, options) && transparentOrderStable) {
            packetStats.assemblyCacheHit = true;
            packetStats.assembledCommandCount = surfaceCommands.size() + edgeCommands.size() +
                                                highlightSurfaceCommands.size() + highlightEdgeCommands.size();
            return {};
        }

        surfaceCommands.clear();
        edgeCommands.clear();
        highlightSurfaceCommands.clear();
        highlightEdgeCommands.clear();
        stats.reset();
        workloadStats.reset();
        workloadStats.visibleObjectCount = visibleIds.size();

        const auto fail = [&](ResultVoid result) -> ResultVoid {
            surfaceCommands.clear();
            edgeCommands.clear();
            highlightSurfaceCommands.clear();
            highlightEdgeCommands.clear();
            invalidateAssembly();
            return result;
        };

        for (RenderObjectId id : visibleIds) {
            const auto known = packets.find(id);
            if (known == packets.end())
                continue;
            const RenderPacket& packet = known->second;
            for (const CachedRenderDrawable& drawable : packet.drawables) {
                ++workloadStats.drawableCount;
                const RenderPassKind pass = renderBucketPass(drawable.bucket);
                const RenderVisualMatch visual = renderVisualMatch(
                        drawable.bucket, packet.pickId, drawable.sourceDrawableIndex, packet.selected, options);

                if (!renderBucketIsOverlay(drawable.bucket)) {
                    if (visual.selected && pass == RenderPassKind::Surface && renderSurfacesEnabled(options)) {
                        ++workloadStats.highlightSurfaceItemCount;
                        ++stats.highlightSurfaceWorkItemCount;
                        auto appended =
                                appendCommand(drawable, true, visual.selected, visual.hovered, highlightSurfaceCommands,
                                              stats.acceptedHighlightSurfaceCommandCount);
                        if (!appended)
                            return fail(appended);
                    }
                    if ((visual.selected || visual.hovered) && pass == RenderPassKind::Edge) {
                        ++workloadStats.highlightEdgeItemCount;
                        ++stats.highlightEdgeWorkItemCount;
                        auto appended = appendCommand(drawable, true, visual.selected, visual.hovered,
                                                      highlightEdgeCommands, stats.acceptedHighlightEdgeCommandCount);
                        if (!appended)
                            return fail(appended);
                    }
                }

                if (!bucketEnabled(drawable.bucket, options, workloadStats))
                    continue;
                switch (pass) {
                case RenderPassKind::Surface: {
                    ++workloadStats.surfaceItemCount;
                    ++stats.surfaceWorkItemCount;
                    auto appended = appendCommand(drawable, false, false, false, surfaceCommands,
                                                  stats.acceptedSurfaceCommandCount);
                    if (!appended)
                        return fail(appended);
                    break;
                }
                case RenderPassKind::Edge: {
                    ++workloadStats.edgeItemCount;
                    ++stats.edgeWorkItemCount;
                    auto appended =
                            appendCommand(drawable, false, false, false, edgeCommands, stats.acceptedEdgeCommandCount);
                    if (!appended)
                        return fail(appended);
                    break;
                }
                case RenderPassKind::None: break;
                }
            }
        }

        sortDrawCommands(surfaceCommands, view ? &view->viewMatrix : nullptr);
        sortDrawCommands(edgeCommands);
        assembledPacketRevision = packetRevision;
        assembledOptions = options;
        assembledViewMatrix = view ? view->viewMatrix : math::Mat4(1.0);
        assembledHasTranslucent = std::ranges::any_of(
                surfaceCommands, [](const MeshDrawCommand& command) { return command.translucent; });
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
    MULAN_PROFILE_ZONE();

    impl_->packetStats.reset();
    const bool rebuild = !impl_->worldVersion || *impl_->worldVersion != snapshot.version() ||
                         !impl_->contextIdentity.matches(context);
    impl_->packetStats.fullRebuild = rebuild;
    if (rebuild) {
        impl_->rebuild(snapshot, context);
        impl_->packetStats.recompiledPacketCount = impl_->packets.size();
    } else {
        impl_->packetStats.cacheHit = true;
        impl_->packetStats.reusedPacketCount = impl_->packets.size();
    }

    const std::vector<RenderObjectId> visibleIds = impl_->visiblePacketIds(view, sceneFrustumCulling);
    return impl_->assemble(options, visibleIds, view);
}

void RenderCompiler::clear() {
    if (!impl_->worldVersion && impl_->surfaceCommands.empty() && impl_->edgeCommands.empty() &&
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
