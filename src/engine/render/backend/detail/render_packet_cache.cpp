#include "render_packet_cache.h"

#include "../render_gpu_contract.h"
#include "../surface_pipeline_provider.h"
#include "../../asset_gpu_registry.h"
#include "../../frontend/render_contract.h"
#include "../../material/material_cache.h"
#include "../../render_geometry.h"

#include <mulan/core/profiling/profile.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace mulan::engine::detail {
namespace {

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
        if (!material)
            cached.materialStatus = DrawableStatus::MaterialRegistrationFailure;
        else
            cached.baseCommand.materialIndex = *material;

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

bool RenderPacketCache::ContextIdentity::matches(const RenderCompileContext& context) const noexcept {
    return assets == &context.assets && materials == &context.materials && assetRevision == context.assets.revision() &&
           materialLayoutRevision == context.materials.layoutRevision() &&
           surfacePipelines == context.surfacePipelines && edgePipeline == context.edgePipeline &&
           highlightSurfacePipeline == context.highlightSurfacePipeline &&
           highlightSurfaceTangentPipeline == context.highlightSurfaceTangentPipeline &&
           highlightEdgePipeline == context.highlightEdgePipeline;
}

RenderPacketCache::ContextIdentity RenderPacketCache::ContextIdentity::capture(
        const RenderCompileContext& context) noexcept {
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

void RenderPacketCache::sync(const RenderWorldSnapshot& snapshot, RenderCompileContext& context) {
    stats_ = {};
    const RenderWorldVersion sourceVersion = snapshot.version();
    const bool packetStateChanged = !worldVersion_ || worldVersion_->world != sourceVersion.world ||
                                    worldVersion_->packetRevision != sourceVersion.packetRevision;
    const bool rebuildRequired = packetStateChanged || !contextIdentity_.matches(context);
    stats_.fullRebuild = rebuildRequired;
    if (rebuildRequired) {
        rebuild(snapshot, context);
        stats_.recompiledPacketCount = packets_.size();
    } else {
        stats_.cacheHit = true;
        stats_.reusedPacketCount = packets_.size();
        worldVersion_ = sourceVersion;
    }
}

void RenderPacketCache::clear() {
    worldVersion_.reset();
    contextIdentity_ = {};
    packets_.clear();
    stats_ = {};
    revision_ = 1;
}

const RenderPacket* RenderPacketCache::find(RenderObjectId id) const {
    const auto known = packets_.find(id);
    return known == packets_.end() ? nullptr : &known->second;
}

void RenderPacketCache::rebuild(const RenderWorldSnapshot& snapshot, RenderCompileContext& context) {
    MULAN_PROFILE_ZONE();

    packets_.clear();
    packets_.reserve(snapshot.objects().size());

    for (const RenderObjectRecord& object : snapshot.objects()) {
        RenderPacket packet = buildPacket(snapshot, object, context);
        packets_.emplace(packet.id, std::move(packet));
    }
    worldVersion_ = snapshot.version();
    contextIdentity_ = ContextIdentity::capture(context);
    advanceRevision();
}

void RenderPacketCache::advanceRevision() noexcept {
    if (++revision_ == 0)
        ++revision_;
}

}  // namespace mulan::engine::detail
