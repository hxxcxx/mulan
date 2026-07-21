/**
 * @file render_workload.h
 * @brief 定义 RenderCompiler 的工作量统计与视觉匹配规则。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_request.h"

#include <cstddef>

namespace mulan::engine {

struct RenderWorkloadStats {
    size_t visibleObjectCount = 0;
    size_t drawableCount = 0;
    size_t surfaceItemCount = 0;
    size_t edgeItemCount = 0;
    size_t highlightSurfaceItemCount = 0;
    size_t highlightEdgeItemCount = 0;
    size_t skippedDisabledSurfaceCount = 0;
    size_t skippedDisabledEdgeCount = 0;
    size_t skippedDisabledOverlayCount = 0;
    size_t skippedUnsupportedBucketCount = 0;

    void reset() { *this = {}; }
};

/// 单个 drawable 在当前选择/悬停状态下的视觉匹配结果。
struct RenderVisualMatch {
    bool selected = false;
    bool hovered = false;
};

/**
 * 计算 drawable 的选择与悬停语义。
 *
 * SelectionVisualState 是选择与悬停视觉的唯一输入。
 */
RenderVisualMatch renderVisualMatch(RenderBucket bucket, PickId pickId, size_t sourceDrawableIndex,
                                    const RenderOptions& options);

}  // namespace mulan::engine
