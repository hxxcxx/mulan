/**
 * @file render_runtime_host.h
 * @brief RenderRuntimeHost 是接入渲染线程前的运行时策略边界。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_runtime.h"

namespace mulan::view {

class RenderRuntimeHost {
public:
    RenderRuntimeHost() = default;
    ~RenderRuntimeHost();

    RenderRuntimeHost(const RenderRuntimeHost&) = delete;
    RenderRuntimeHost& operator=(const RenderRuntimeHost&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height, engine::LightEnvironment& lightEnv);

    core::Result<void> initOffscreen(int width, int height, engine::LightEnvironment& lightEnv);

    void shutdown();

    bool isInitialized() const;

    void setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);

    void render(const ViewState& viewState);
    void resize(int width, int height);
    void enableIBL(const std::string& hdrPath);

    bool isOffscreenSurface() const;
    uint32_t surfaceWidth() const;
    uint32_t surfaceHeight() const;

    bool readbackPixels(std::vector<uint8_t>& pixels);
    bool configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    bool configureOffscreenSurface(const RenderSurfaceDesc& desc);
    std::optional<RenderSurfaceDesc> offscreenSurfaceDesc() const;
    const RenderWorldSyncStats& lastWorldSyncStats() const;

private:
    RenderRuntime runtime_;
};

}  // namespace mulan::view
