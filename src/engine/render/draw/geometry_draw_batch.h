/**
 * @file geometry_draw_batch.h
 * @brief 对已排序 MeshDrawCommand 生成保守、确定性的对象实例批计划。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * 规划器不改写命令，也不接触 GPU。只有完整绘制状态相同且显式允许实例化的
 * 连续命令才会合批；world/pick/selected/hovered 作为逐实例 ObjectUniforms 保留。
 */

#pragma once

#include "../mesh_draw_command.h"
#include "../gpu_scene_contract.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mulan::engine {

inline constexpr size_t kObjectBatchMinSize = 8;

struct GeometryDrawBatchRange {
    size_t first = 0;
    size_t count = 0;
    bool instanced = false;

    friend constexpr bool operator==(const GeometryDrawBatchRange&, const GeometryDrawBatchRange&) = default;
};

/// 判断命令是否允许进入指定执行器的实例化路径。
bool isGeometryDrawBatchCandidate(const MeshDrawCommand& command, const PipelineState* executorPipeline) noexcept;

/// 比较实例 draw 的完整共享状态；不依赖可能碰撞的 sortKey。
bool geometryDrawBatchCompatible(const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) noexcept;

/// 输出覆盖输入区间且保持原顺序的计划；legacy 相邻范围会合并。
void planGeometryDrawBatches(std::span<const MeshDrawCommand> commands, const PipelineState* executorPipeline,
                             bool instancingAvailable, std::vector<GeometryDrawBatchRange>& out);

/// 按命令原顺序生成完整固定大小对象块；空输入或超过容量时拒绝且不产生残缺批。
bool packGeometryDrawObjectBatch(std::span<const MeshDrawCommand> commands, ObjectBatchUniforms& out) noexcept;

}  // namespace mulan::engine
