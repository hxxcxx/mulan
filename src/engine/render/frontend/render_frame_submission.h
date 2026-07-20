/**
 * @file render_frame_submission.h
 * @brief 定义跨线程提交给渲染运行时的自持有不可变帧输入。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include "render_request.h"
#include "render_resource_prepare.h"
#include "render_world_snapshot.h"
#include "../light_environment.h"

#include <cstdint>
#include <memory>

namespace mulan::engine {

struct RenderFrameSubmission {
    std::shared_ptr<const RenderWorldSnapshot> sceneWorld;
    std::shared_ptr<const RenderWorldSnapshot> overlayWorld;
    RenderResourcePrepareList prepare;
    RenderViewDesc view;
    RenderOptions options;
    LightEnvironment lighting;
    uint64_t resourceBatchId = 0;

    bool hasWorld() const { return static_cast<bool>(sceneWorld) || static_cast<bool>(overlayWorld); }
    bool hasResourceUpdates() const { return resourceBatchId != 0 && !prepare.empty(); }

    RenderRequest request() const {
        return RenderRequest{
            .sceneWorld = sceneWorld.get(),
            .overlayWorld = overlayWorld.get(),
            .view = view,
            .options = options,
        };
    }
};

}  // namespace mulan::engine
