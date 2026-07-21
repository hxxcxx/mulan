/**
 * @file render_packet_cache.h
 * @brief 管理世界版本化 RenderPacket 缓存及其 GPU 编译上下文身份。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include "render_packet.h"
#include "../render_compile_context.h"
#include "../../frontend/render_world_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace mulan::engine::detail {

struct PacketCacheSyncStats {
    bool cacheHit = false;
    bool fullRebuild = false;
    size_t recompiledPacketCount = 0;
    size_t reusedPacketCount = 0;
};

class RenderPacketCache {
public:
    void sync(const RenderWorldSnapshot& snapshot, RenderCompileContext& context);
    void clear();

    const RenderPacket* find(RenderObjectId id) const;
    const PacketCacheSyncStats& lastStats() const { return stats_; }
    uint64_t revision() const { return revision_; }
    bool hasState() const { return worldVersion_.has_value(); }

private:
    using ObjectIdHash = RenderHandleHash<RenderObjectIdTag>;
    using PacketMap = std::unordered_map<RenderObjectId, RenderPacket, ObjectIdHash>;

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

        bool matches(const RenderCompileContext& context) const noexcept;
        static ContextIdentity capture(const RenderCompileContext& context) noexcept;
    };

    void rebuild(const RenderWorldSnapshot& snapshot, RenderCompileContext& context);
    void advanceRevision() noexcept;

    std::optional<RenderWorldVersion> worldVersion_;
    ContextIdentity contextIdentity_;
    PacketMap packets_;
    PacketCacheSyncStats stats_;
    uint64_t revision_ = 1;
};

}  // namespace mulan::engine::detail
