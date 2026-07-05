/**
 * @file render_frame.h
 * @brief RenderFrame 聚合 RHI 设备、命令列表、视图和目标信息。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_target_info.h"
#include "render_view.h"

namespace mulan::engine {

class CommandList;
class RHIDevice;

struct RenderFrame {
    RHIDevice& device;
    CommandList& cmd;
    RenderView view;
    RenderTargetInfo target;
};

} // namespace mulan::engine
