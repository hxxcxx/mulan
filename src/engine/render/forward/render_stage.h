/**
 * @file render_stage.h
 * @brief RenderStage 定义 engine backend 中渲染阶段的统一接口。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "../frame/render_frame.h"
#include "../frame/render_target_info.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <string_view>

namespace mulan::engine {

class RHIDevice;

class RenderStage {
public:
    virtual ~RenderStage() = default;

    virtual std::string_view name() const = 0;

    virtual core::Result<void> init(RHIDevice& device, const RenderTargetInfo& target) = 0;

    virtual void shutdown(RHIDevice& device) = 0;
    virtual void resize(RHIDevice& device, const RenderTargetInfo& target) {
        (void) device;
        (void) target;
    }
    /// 仅录制当前帧 draw/dispatch 命令，不创建或销毁持久 GPU 资源。
    /// 持久资源准备应放在 init/resize 或 RenderRenderer::prepareResources 阶段。
    virtual void execute(RenderFrame& frame) = 0;
};

}  // namespace mulan::engine
