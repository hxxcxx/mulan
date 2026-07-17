/**
 * @file render_frame.h
 * @brief RenderFrame 聚合当前帧命令列表和只读视图状态。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_view.h"

namespace mulan::engine {

class CommandList;

struct RenderFrame {
    CommandList& cmd;
    RenderView view;
};

}  // namespace mulan::engine
