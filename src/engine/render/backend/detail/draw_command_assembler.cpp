#include "draw_command_assembler.h"

#include "../../frontend/render_contract.h"
#include "../../frontend/render_workload.h"
#include "../../../rhi/engine_error_code.h"

#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <ranges>
#include <utility>

namespace mulan::engine::detail {
namespace {

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
    if (lhs.displayMode != rhs.displayMode || lhs.showOverlays != rhs.showOverlays) {
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

bool sameIds(std::span<const RenderObjectId> lhs, const std::vector<RenderObjectId>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

}  // namespace

ResultVoid DrawCommandAssembler::assemble(const RenderPacketCache& packets, std::span<const RenderObjectId> visibleIds,
                                          const RenderOptions& options, const RenderViewDesc* view) {
    MULAN_PROFILE_ZONE();

    cacheStats_ = {};
    const bool transparentOrderStable =
            !assembledHasTranslucent_ || !view || matricesEqual(assembledViewMatrix_, view->viewMatrix);
    if (assembledOptions_ && assembledPacketRevision_ == packets.revision() &&
        sameIds(visibleIds, assembledVisibleIds_) && commandOptionsEqual(*assembledOptions_, options) &&
        transparentOrderStable) {
        cacheStats_.cacheHit = true;
        cacheStats_.assembledCommandCount = surfaceCommands_.size() + edgeCommands_.size() +
                                            highlightSurfaceCommands_.size() + highlightEdgeCommands_.size();
        return {};
    }

    surfaceCommands_.clear();
    edgeCommands_.clear();
    highlightSurfaceCommands_.clear();
    highlightEdgeCommands_.clear();
    stats_.reset();
    workloadStats_.reset();
    workloadStats_.visibleObjectCount = visibleIds.size();

    const auto fail = [&](ResultVoid result) -> ResultVoid {
        surfaceCommands_.clear();
        edgeCommands_.clear();
        highlightSurfaceCommands_.clear();
        highlightEdgeCommands_.clear();
        invalidateCache();
        return result;
    };

    for (RenderObjectId id : visibleIds) {
        const RenderPacket* packet = packets.find(id);
        if (!packet)
            continue;
        for (const CachedRenderDrawable& drawable : packet->drawables) {
            ++workloadStats_.drawableCount;
            const RenderPassKind pass = renderBucketPass(drawable.bucket);
            const RenderVisualMatch visual =
                    renderVisualMatch(drawable.bucket, packet->pickId, drawable.sourceDrawableIndex, options);

            if (!renderBucketIsOverlay(drawable.bucket)) {
                if (visual.selected && pass == RenderPassKind::Surface && renderSurfacesEnabled(options)) {
                    ++workloadStats_.highlightSurfaceItemCount;
                    ++stats_.highlightSurfaceWorkItemCount;
                    auto appended =
                            appendCommand(drawable, true, visual.selected, visual.hovered, highlightSurfaceCommands_,
                                          stats_.acceptedHighlightSurfaceCommandCount);
                    if (!appended)
                        return fail(appended);
                }
                if ((visual.selected || visual.hovered) && pass == RenderPassKind::Edge) {
                    ++workloadStats_.highlightEdgeItemCount;
                    ++stats_.highlightEdgeWorkItemCount;
                    auto appended = appendCommand(drawable, true, visual.selected, visual.hovered,
                                                  highlightEdgeCommands_, stats_.acceptedHighlightEdgeCommandCount);
                    if (!appended)
                        return fail(appended);
                }
            }

            if (!bucketEnabled(drawable.bucket, options, workloadStats_))
                continue;
            switch (pass) {
            case RenderPassKind::Surface: {
                ++workloadStats_.surfaceItemCount;
                ++stats_.surfaceWorkItemCount;
                auto appended = appendCommand(drawable, false, false, false, surfaceCommands_,
                                              stats_.acceptedSurfaceCommandCount);
                if (!appended)
                    return fail(appended);
                break;
            }
            case RenderPassKind::Edge: {
                ++workloadStats_.edgeItemCount;
                ++stats_.edgeWorkItemCount;
                auto appended =
                        appendCommand(drawable, false, false, false, edgeCommands_, stats_.acceptedEdgeCommandCount);
                if (!appended)
                    return fail(appended);
                break;
            }
            case RenderPassKind::None: break;
            }
        }
    }

    sortDrawCommands(surfaceCommands_, view ? &view->viewMatrix : nullptr);
    sortDrawCommands(edgeCommands_);
    assembledPacketRevision_ = packets.revision();
    assembledOptions_ = options;
    assembledViewMatrix_ = view ? view->viewMatrix : math::Mat4(1.0);
    assembledHasTranslucent_ =
            std::ranges::any_of(surfaceCommands_, [](const MeshDrawCommand& command) { return command.translucent; });
    assembledVisibleIds_.assign(visibleIds.begin(), visibleIds.end());
    advanceCommandRevision();
    cacheStats_.assembledCommandCount = surfaceCommands_.size() + edgeCommands_.size() +
                                        highlightSurfaceCommands_.size() + highlightEdgeCommands_.size();
    return {};
}

void DrawCommandAssembler::clear() {
    surfaceCommands_.clear();
    edgeCommands_.clear();
    highlightSurfaceCommands_.clear();
    highlightEdgeCommands_.clear();
    stats_.reset();
    workloadStats_.reset();
    cacheStats_ = {};
    invalidateCache();
    advanceCommandRevision();
}

bool DrawCommandAssembler::empty() const {
    return surfaceCommands_.empty() && edgeCommands_.empty() && highlightSurfaceCommands_.empty() &&
           highlightEdgeCommands_.empty();
}

ResultVoid DrawCommandAssembler::appendCommand(const CachedRenderDrawable& drawable, bool highlight, bool selected,
                                               bool hovered, std::vector<MeshDrawCommand>& destination,
                                               size_t& acceptedCount) {
    if (drawable.geometryStatus != DrawableStatus::Ready) {
        recordDrawableFailure(drawable.geometryStatus, stats_);
        if (drawable.geometryStatus == DrawableStatus::MissingGpuGeometry) {
            return std::unexpected(
                    Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU geometry."));
        }
        return {};
    }
    PipelineState* pipeline = highlight ? drawable.highlightPipeline : drawable.baseCommand.pipelineState;
    if (!pipeline) {
        ++stats_.missingPipelineCount;
        return {};
    }
    if (drawable.materialStatus != DrawableStatus::Ready) {
        recordDrawableFailure(drawable.materialStatus, stats_);
        return {};
    }
    if (!highlight && drawable.textureStatus == DrawableStatus::MissingGpuTexture) {
        stats_.missingGpuTextureCount += drawable.missingTextureCount;
        return std::unexpected(Error::make(ErrorCode::NotFound, "Render packet references an unprepared GPU texture."));
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

void DrawCommandAssembler::invalidateCache() {
    assembledPacketRevision_ = 0;
    assembledOptions_.reset();
    assembledVisibleIds_.clear();
    assembledHasTranslucent_ = false;
}

void DrawCommandAssembler::advanceCommandRevision() noexcept {
    if (++commandRevision_ == 0)
        ++commandRevision_;
}

}  // namespace mulan::engine::detail
