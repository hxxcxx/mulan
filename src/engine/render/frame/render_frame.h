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
