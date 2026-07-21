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

/**
 * 一次成功命令组装结果的只读视图。
 *
 * 四个 span 均借用 RenderCompiler 内部存储，只在对应 compiler 的下一次
 * compile()/clear() 前有效。revision 始终非零，仅在该组命令真正重新发布时变化，
 * 使下游可以按 Scene/Overlay 来源独立更新自己的命令缓存。
 */
struct CompiledDrawCommandSet {
    uint64_t revision = 0;
    std::span<const MeshDrawCommand> surfaces;
    std::span<const MeshDrawCommand> edges;
    std::span<const MeshDrawCommand> highlightSurfaces;
    std::span<const MeshDrawCommand> highlightEdges;
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

    CompiledDrawCommandSet drawCommands() const;
    std::span<const MeshDrawCommand> surfaceCommands() const;
    std::span<const MeshDrawCommand> edgeCommands() const;
    std::span<const MeshDrawCommand> highlightSurfaceCommands() const;
    std::span<const MeshDrawCommand> highlightEdgeCommands() const;
    const RenderCompilerStats& lastStats() const;
    const RenderWorkloadStats& lastWorkloadStats() const;
    const RenderPacketCacheStats& lastPacketCacheStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mulan::engine
