/**
 * @file render_compiler.h
 * @brief RenderCompiler 按世界版本缓存 RenderPacket，并组装当前可见绘制命令。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_compile_context.h"
#include "../mesh_draw_command.h"
#include "../frontend/render_request.h"
#include "../frontend/render_workload.h"

#include <mulan/core/result/error.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mulan::engine {

struct RenderCompilerStats {
    size_t surfaceWorkItemCount = 0;
    size_t edgeWorkItemCount = 0;
    size_t highlightSurfaceWorkItemCount = 0;
    size_t highlightEdgeWorkItemCount = 0;
    size_t acceptedSurfaceCommandCount = 0;
    size_t acceptedEdgeCommandCount = 0;
    size_t acceptedHighlightSurfaceCommandCount = 0;
    size_t acceptedHighlightEdgeCommandCount = 0;
    size_t missingGeometryRecordCount = 0;
    size_t emptyGeometryCount = 0;
    size_t missingGpuGeometryCount = 0;
    size_t missingGpuTextureCount = 0;
    size_t rejectedContractCount = 0;
    size_t missingPipelineCount = 0;
    size_t materialRegistrationFailureCount = 0;

    void reset() { *this = {}; }
};

/// 世界级 Packet 缓存与视锥查询的逐帧诊断数据。
struct RenderPacketCacheStats {
    bool cacheHit = false;
    bool assemblyCacheHit = false;
    bool fullRebuild = false;
    bool bvhRebuilt = false;
    bool frustumFailOpen = false;
    size_t recompiledPacketCount = 0;
    size_t reusedPacketCount = 0;
    size_t sourceVisibleObjectCount = 0;
    size_t frustumVisibleObjectCount = 0;
    size_t culledObjectCount = 0;
    size_t uncullableObjectCount = 0;
    size_t bvhNodeBoundsTestCount = 0;
    size_t bvhLeafBoundsTestCount = 0;
    size_t assembledCommandCount = 0;

    void reset() { *this = {}; }
};

class RenderCompiler {
public:
    RenderCompiler();
    ~RenderCompiler();

    RenderCompiler(const RenderCompiler&) = delete;
    RenderCompiler& operator=(const RenderCompiler&) = delete;

    /**
     * 同步世界级 Packet 缓存并组装命令。
     *
     * sceneFrustumCulling 为 true 时 view 必须描述 SceneWorld 的 CPU 视锥；非法矩阵
     * 按 fail-open 处理。OverlayWorld 传 false，保持预览、夹点和 Gizmo 的既有语义。
     * Packet 与 BVH 使用独立的世界版本域：材质、GPU 资源或编译上下文变化不会
     * 重建 BVH；纯可见性变化不会重编 Packet。相机和视觉状态变化只重新查询
     * 可见性并组装命令。返回失败时命令列表为空。
     */
    ResultVoid compile(const RenderWorldSnapshot& snapshot, const RenderOptions& options, RenderCompileContext& context,
                       const RenderViewDesc* view, bool sceneFrustumCulling);

    void clear();

    std::span<const MeshDrawCommand> surfaceCommands() const;
    std::span<const MeshDrawCommand> edgeCommands() const;
    std::span<const MeshDrawCommand> highlightSurfaceCommands() const;
    std::span<const MeshDrawCommand> highlightEdgeCommands() const;
    const RenderCompilerStats& lastStats() const;
    const RenderWorkloadStats& lastWorkloadStats() const;
    const RenderPacketCacheStats& lastPacketCacheStats() const;
    /// 成功发布新的命令组装结果时推进。
    uint64_t commandRevision() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mulan::engine
