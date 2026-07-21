/**
 * @file draw_command_assembler.h
 * @brief 根据可见 RenderPacket 组装、排序并缓存四类 MeshDrawCommand。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include "render_packet_cache.h"
#include "../render_compiler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mulan::engine::detail {

struct AssemblyCacheStats {
    bool cacheHit = false;
    size_t assembledCommandCount = 0;
};

class DrawCommandAssembler {
public:
    ResultVoid assemble(const RenderPacketCache& packets, std::span<const RenderObjectId> visibleIds,
                        const RenderOptions& options, const RenderViewDesc* view);
    void clear();

    std::span<const MeshDrawCommand> surfaceCommands() const { return surfaceCommands_; }
    std::span<const MeshDrawCommand> edgeCommands() const { return edgeCommands_; }
    std::span<const MeshDrawCommand> highlightSurfaceCommands() const { return highlightSurfaceCommands_; }
    std::span<const MeshDrawCommand> highlightEdgeCommands() const { return highlightEdgeCommands_; }
    const RenderCompilerStats& lastStats() const { return stats_; }
    const RenderWorkloadStats& lastWorkloadStats() const { return workloadStats_; }
    const AssemblyCacheStats& lastCacheStats() const { return cacheStats_; }
    uint64_t commandRevision() const { return commandRevision_; }
    bool empty() const;

private:
    ResultVoid appendCommand(const CachedRenderDrawable& drawable, bool highlight, bool selected, bool hovered,
                             std::vector<MeshDrawCommand>& destination, size_t& acceptedCount);
    void invalidateCache();
    void advanceCommandRevision() noexcept;

    uint64_t assembledPacketRevision_ = 0;
    std::optional<RenderOptions> assembledOptions_;
    math::Mat4 assembledViewMatrix_{ 1.0 };
    bool assembledHasTranslucent_ = false;
    std::vector<RenderObjectId> assembledVisibleIds_;

    std::vector<MeshDrawCommand> surfaceCommands_;
    std::vector<MeshDrawCommand> edgeCommands_;
    std::vector<MeshDrawCommand> highlightSurfaceCommands_;
    std::vector<MeshDrawCommand> highlightEdgeCommands_;
    RenderCompilerStats stats_;
    RenderWorkloadStats workloadStats_;
    AssemblyCacheStats cacheStats_;
    uint64_t commandRevision_ = 1;
};

}  // namespace mulan::engine::detail
