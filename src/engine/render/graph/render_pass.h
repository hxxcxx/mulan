/**
 * @file render_pass.h
 * @brief 渲染 Pass 基类 — RenderGraph 的执行单元
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../../rhi/command_list.h"
#include <mulan/math/math.h>

namespace mulan::engine {

/// 一帧的相机派生数据（view/proj 矩阵 + 视点位置），由上层 ViewState 填充。
/// passes 从 PassContext 读取，不再反向访问活 Camera 对象。
struct CameraView {
    Mat4 viewMatrix       = Mat4(1.0);
    Mat4 projectionMatrix = Mat4(1.0);
    Vec3 eyePosition      = Vec3(0.0f);
};

struct PassContext {
    CommandList* cmd       = nullptr;
    int          width     = 0;
    int          height    = 0;
    CameraView   camera;     // 当帧相机快照
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual const char* name() const = 0;
    virtual void execute(const PassContext& ctx) = 0;
};

} // namespace mulan::engine
