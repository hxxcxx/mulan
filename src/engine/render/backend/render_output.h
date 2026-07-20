/**
 * @file render_output.h
 * @brief ForwardRenderer 单次调用使用的非拥有渲染输出。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

namespace mulan::engine {

class RenderTarget;
class SwapChain;

class RenderOutput {
public:
    explicit RenderOutput(SwapChain& swapChain) : swap_chain_(&swapChain) {}
    explicit RenderOutput(RenderTarget& renderTarget) : render_target_(&renderTarget) {}

    bool isPresentable() const { return swap_chain_ != nullptr; }
    SwapChain* swapChain() const { return swap_chain_; }
    RenderTarget* renderTarget() const { return render_target_; }

private:
    SwapChain* swap_chain_ = nullptr;
    RenderTarget* render_target_ = nullptr;
};

}  // namespace mulan::engine
