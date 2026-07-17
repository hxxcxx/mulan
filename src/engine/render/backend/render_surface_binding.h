/**
 * @file render_surface_binding.h
 * @brief RenderSurfaceBinding 将窗口交换链或离屏目标以 engine backend 可消费的形式传入。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

namespace mulan::engine {

class RenderTarget;
class SwapChain;

struct RenderSurfaceBinding {
    SwapChain* swapChain = nullptr;
    RenderTarget* renderTarget = nullptr;

    bool isValid() const { return (swapChain != nullptr) != (renderTarget != nullptr); }
    bool isPresentable() const { return swapChain != nullptr && renderTarget == nullptr; }
};

}  // namespace mulan::engine
