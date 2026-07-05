#pragma once

namespace mulan::engine {

class RenderTarget;
class SwapChain;

struct RenderSurfaceBinding {
    SwapChain* swapChain = nullptr;
    RenderTarget* renderTarget = nullptr;

    bool isValid() const { return swapChain || renderTarget; }
    bool isOffscreen() const { return !swapChain && renderTarget; }
};

} // namespace mulan::engine
