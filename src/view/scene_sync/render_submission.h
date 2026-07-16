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

#include <mulan/view/core/view_state.h>

#include <mulan/render/frontend/render_resource_prepare.h>
#include <mulan/render/frontend/render_world_snapshot.h>
#include <mulan/render/light_environment.h>

#include <cstdint>
#include <memory>

namespace mulan::view {

struct RenderSubmission {
    /// 低频文档场景快照；预览、选择和相机变化不会替换它。
    std::shared_ptr<const engine::RenderWorldSnapshot> sceneWorld;
    /// 高频工具覆盖层快照；生命周期与 SceneWorld 完全独立。
    std::shared_ptr<const engine::RenderWorldSnapshot> overlayWorld;
    /// 仅包含本次世界更新所需的 GPU 上传快照。
    engine::RenderResourcePrepareList prepare;
    /// 相机、显示模式、选择和 overlay 等当帧值状态。
    ViewState view;
    /// 光照值快照；渲染端不再引用 ViewContext 的可变环境。
    engine::LightEnvironment lightEnvironment;
    /// 当前携带的待确认 GPU 资源批次；0 表示没有持久资源更新。
    uint64_t resourceBatchId = 0;

    bool hasWorld() const { return static_cast<bool>(sceneWorld) || static_cast<bool>(overlayWorld); }
    bool hasResourceUpdates() const { return resourceBatchId != 0 && !prepare.empty(); }
};

}  // namespace mulan::view
