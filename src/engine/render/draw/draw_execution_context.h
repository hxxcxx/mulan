/**
 * @file draw_execution_context.h
 * @brief draw executor 使用的轻量执行上下文
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../../rhi/command_list.h"
#include <mulan/math/math.h>

namespace mulan::engine {

/// 一帧的相机派生数据（view/proj 矩阵 + 视点位置），由上层 ViewState 填充。
/// draw executor 从 DrawExecutionContext 读取，不反向访问活 Camera 对象。
struct DrawCameraSnapshot {
    math::Mat4 viewMatrix       = math::Mat4(1.0);
    math::Mat4 projectionMatrix = math::Mat4(1.0);
    math::Vec3 eyePosition      = math::Vec3(0.0f);
};

struct DrawExecutionContext {
    CommandList* cmd       = nullptr;
    int          width     = 0;
    int          height    = 0;
    DrawCameraSnapshot camera;
};

class DrawExecutor {
public:
    virtual ~DrawExecutor() = default;

    virtual const char* name() const = 0;
    virtual void execute(const DrawExecutionContext& ctx) = 0;
};

} // namespace mulan::engine
