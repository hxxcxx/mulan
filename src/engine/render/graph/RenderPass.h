/**
 * @file RenderPass.h
 * @brief 渲染 Pass 基类 — RenderGraph 的执行单元
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../../rhi/CommandList.h"

namespace mulan::engine {

struct PassContext {
    CommandList* cmd       = nullptr;
    int          width     = 0;
    int          height    = 0;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    virtual const char* name() const = 0;
    virtual void execute(const PassContext& ctx) = 0;
};

} // namespace mulan::engine
