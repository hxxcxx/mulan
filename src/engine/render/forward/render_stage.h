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

    virtual std::expected<void, core::Error>
    init(RHIDevice& device, const RenderTargetInfo& target) = 0;

    virtual void shutdown(RHIDevice& device) = 0;
    virtual void resize(RHIDevice& device, const RenderTargetInfo& target) {
        (void)device;
        (void)target;
    }
    virtual void execute(RenderFrame& frame) = 0;
};

} // namespace mulan::engine
