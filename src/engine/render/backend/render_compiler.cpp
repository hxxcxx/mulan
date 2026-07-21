#include "render_compiler.h"

#include "detail/draw_command_assembler.h"
#include "detail/render_packet_cache.h"
#include "detail/render_visibility_index.h"

#include <mulan/core/profiling/profile.h>

#include <utility>

namespace mulan::engine {

struct RenderCompiler::Impl {
    detail::RenderPacketCache packetCache;
    detail::RenderVisibilityIndex visibilityIndex;
    detail::DrawCommandAssembler commandAssembler;
    RenderPacketCacheStats combinedStats;

    void collectStats(bool bvhRebuilt) {
        const detail::PacketCacheSyncStats& packet = packetCache.lastStats();
        const detail::VisibilityQueryStats& visibility = visibilityIndex.lastStats();
        const detail::AssemblyCacheStats& assembly = commandAssembler.lastCacheStats();
        combinedStats = {
            .cacheHit = packet.cacheHit,
            .assemblyCacheHit = assembly.cacheHit,
            .fullRebuild = packet.fullRebuild,
            .bvhRebuilt = bvhRebuilt,
            .frustumFailOpen = visibility.frustumFailOpen,
            .recompiledPacketCount = packet.recompiledPacketCount,
            .reusedPacketCount = packet.reusedPacketCount,
            .sourceVisibleObjectCount = visibility.sourceVisibleObjectCount,
            .frustumVisibleObjectCount = visibility.frustumVisibleObjectCount,
            .culledObjectCount = visibility.culledObjectCount,
            .uncullableObjectCount = visibility.uncullableObjectCount,
            .bvhNodeBoundsTestCount = visibility.bvhNodeBoundsTestCount,
            .bvhLeafBoundsTestCount = visibility.bvhLeafBoundsTestCount,
            .assembledCommandCount = assembly.assembledCommandCount,
        };
    }
};

RenderCompiler::RenderCompiler() : impl_(std::make_unique<Impl>()) {
}

RenderCompiler::~RenderCompiler() = default;

ResultVoid RenderCompiler::compile(const RenderWorldSnapshot& snapshot, const RenderOptions& options,
                                   RenderCompileContext& context, const RenderViewDesc* view,
                                   bool sceneFrustumCulling) {
    MULAN_PROFILE_ZONE();

    impl_->packetCache.sync(snapshot, context);
    const bool bvhRebuilt = impl_->visibilityIndex.sync(snapshot);

    const std::span<const RenderObjectId> visibleIds = impl_->visibilityIndex.resolve(view, sceneFrustumCulling);
    ResultVoid assembled = impl_->commandAssembler.assemble(impl_->packetCache, visibleIds, options, view);
    impl_->collectStats(bvhRebuilt);
    return assembled;
}

void RenderCompiler::clear() {
    if (!impl_->packetCache.hasState() && impl_->commandAssembler.empty())
        return;

    impl_->packetCache.clear();
    impl_->visibilityIndex.clear();
    impl_->commandAssembler.clear();
    impl_->combinedStats.reset();
}

std::span<const MeshDrawCommand> RenderCompiler::surfaceCommands() const {
    return impl_->commandAssembler.surfaceCommands();
}

std::span<const MeshDrawCommand> RenderCompiler::edgeCommands() const {
    return impl_->commandAssembler.edgeCommands();
}

std::span<const MeshDrawCommand> RenderCompiler::highlightSurfaceCommands() const {
    return impl_->commandAssembler.highlightSurfaceCommands();
}

std::span<const MeshDrawCommand> RenderCompiler::highlightEdgeCommands() const {
    return impl_->commandAssembler.highlightEdgeCommands();
}

const RenderCompilerStats& RenderCompiler::lastStats() const {
    return impl_->commandAssembler.lastStats();
}

const RenderWorkloadStats& RenderCompiler::lastWorkloadStats() const {
    return impl_->commandAssembler.lastWorkloadStats();
}

const RenderPacketCacheStats& RenderCompiler::lastPacketCacheStats() const {
    return impl_->combinedStats;
}

uint64_t RenderCompiler::commandRevision() const {
    return impl_->commandAssembler.commandRevision();
}

}  // namespace mulan::engine
