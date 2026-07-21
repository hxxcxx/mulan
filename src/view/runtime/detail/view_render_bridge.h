/**
 * @file view_render_bridge.h
 * @brief ViewRenderBridge 将 view 活对象投影为 render-owned 帧提交。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include "../../core/view_config.h"
#include "../../core/view_state.h"
#include "../../scene_sync/projection/render_submission_builder.h"

#include <mulan/render/runtime/render_session.h>

#include <functional>
#include <memory>
#include <string>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class PreviewLayer;
class RenderScene;

namespace detail {

class ViewRenderBridge {
public:
    ViewRenderBridge();
    ~ViewRenderBridge();

    ViewRenderBridge(const ViewRenderBridge&) = delete;
    ViewRenderBridge& operator=(const ViewRenderBridge&) = delete;

    ResultVoid init(const ViewConfig& config, int width, int height, std::function<void()> runtimeEventCallback);
    void shutdown();
    bool isReady() const;
    ResultVoid consumeRuntimeEvents();

    void setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);
    void setLightEnvironment(const engine::LightEnvironment& lightEnvironment);

    ResultVoid submitFrame(const ViewState& viewState);
    Result<engine::RenderCaptureResult> capture(const ViewState& viewState, const engine::RenderCaptureDesc& desc);
    engine::RenderSurfaceState resize(int width, int height);
    void enableIBL(const std::string& hdrPath);
    engine::RenderSurfaceState surfaceState() const;

private:
    RenderSubmissionBuilder submission_builder_;
    std::unique_ptr<engine::RenderSession> session_;
    const asset::AssetLibrary* asset_source_ = nullptr;
};

}  // namespace detail
}  // namespace mulan::view
