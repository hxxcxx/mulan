/**
 * @file render_runtime_host.h
 * @brief RenderRuntimeHost 是接入渲染线程前的运行时策略边界。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <mulan/view/runtime/render_runtime.h>
#include <mulan/view/scene_sync/render_submission_builder.h>
#include <mulan/view/runtime/threaded_render_runtime.h>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class PreviewLayer;
class RenderScene;

class RenderRuntimeHost {
public:
    RenderRuntimeHost() = default;
    ~RenderRuntimeHost();

    RenderRuntimeHost(const RenderRuntimeHost&) = delete;
    RenderRuntimeHost& operator=(const RenderRuntimeHost&) = delete;

    core::Result<void> initWindow(const ViewConfig& config, int width, int height);

    core::Result<void> initOffscreen(const ViewConfig& config, int width, int height);
    core::Result<void> initOffscreen(int width, int height);

    void shutdown();

    bool isInitialized() const;

    void setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);
    void setLightEnvironment(const engine::LightEnvironment& lightEnvironment);

    void render(const ViewState& viewState);
    core::Result<engine::RenderCaptureResult> capture(const ViewState& viewState,
                                                      const engine::RenderCaptureDesc& desc);
    void resize(int width, int height);
    void enableIBL(const std::string& hdrPath);

    uint32_t surfaceWidth() const;
    uint32_t surfaceHeight() const;

    const RenderWorldSyncStats& lastWorldSyncStats() const;
    const RenderSubmissionDiagnostics& renderSubmissionDiagnostics() const;
    const engine::RenderWorkloadStats& lastRenderWorkloadStats() const;
    const engine::RenderCompilerStats& lastRenderCompilerStats() const;

private:
    RenderSubmissionBuilder submission_builder_;
    std::shared_ptr<RenderRuntime> runtime_ = std::make_shared<RenderRuntime>();
    std::unique_ptr<ThreadedRenderRuntime> threaded_runtime_;
    const asset::AssetLibrary* asset_source_ = nullptr;
};

}  // namespace mulan::view
