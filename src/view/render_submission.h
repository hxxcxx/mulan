/**
 * @file render_submission.h
 * @brief RenderSubmission 定义一次渲染所消费的自持有输入快照。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 提交对象由 view 主线程构建。它不引用 RenderScene、AssetLibrary 或 PreviewLayer，
 * 因而可保持为同步路径输入，并为后续渲染线程队列提供稳定边界。
 */

#pragma once

#include "render_world_sync.h"
#include "view_state.h"

#include <mulan/engine/render/frontend/render_resource_prepare.h>
#include <mulan/engine/render/frontend/render_world_snapshot.h>

#include <cstdint>
#include <memory>

namespace mulan::view {

struct RenderSubmission {
    /// 不可变世界快照，可被多个提交安全共享。
    std::shared_ptr<const engine::RenderWorldSnapshot> world;
    /// 仅包含本次世界更新所需的 GPU 上传快照。
    engine::RenderResourcePrepareList prepare;
    /// 相机、显示模式、选择和 overlay 等当帧值状态。
    ViewState view;
    RenderWorldSyncStats syncStats;
    uint64_t sceneGeneration = 0;
    uint64_t geometryGeneration = 0;
    uint64_t previewGeneration = 0;
    uint64_t surfaceGeneration = 0;
    bool rebuiltWorld = false;
    uint64_t generation = 0;

    bool hasWorld() const { return static_cast<bool>(world); }
    bool hasResourceUpdates() const { return !prepare.geometries().empty(); }
};

}  // namespace mulan::view
