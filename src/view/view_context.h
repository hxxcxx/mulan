/**
 * @file view_context.h
 * @brief ViewContext 管理视图状态、相机和交互，并生成 ViewState。
 * @author hxxcxx
 */
#pragma once

#include "render_runtime_host.h"
#include "view_config.h"
#include "view_state.h"

#include "capture_batch.h"

#include "mulan/core/result/error.h"
#include "mulan/engine/interaction/camera_manipulator.h"
#include "mulan/engine/interaction/input_event.h"
#include "mulan/engine/interaction/operator.h"
#include "mulan/engine/render/light_environment.h"
#include "mulan/engine/render/camera/camera.h"
#include "mulan/engine/render/overlay/view_cube_model.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class RenderScene;
}

namespace mulan::view {

class ViewContext {
public:
    ViewContext();
    ~ViewContext();

    ViewContext(const ViewContext&) = delete;
    ViewContext& operator=(const ViewContext&) = delete;

    bool init(const ViewConfig& config, int width, int height);
    bool initOffscreen(int width, int height);
    void shutdown();

    bool isInitialized() const { return runtime_host_.isInitialized(); }

    void setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets);

    void renderFrame();
    void resize(int width, int height);

    void handleInput(const engine::InputEvent& event);

    void pushOperator(std::unique_ptr<engine::Operator> op);
    void popOperator();

    engine::Operator* activeOperator() const;
    engine::Operator* defaultOperator() const { return default_op_.get(); }

    core::Result<engine::RenderCaptureResult> capture(const engine::RenderCaptureDesc& desc);
    core::Result<CaptureImage> capture(const CaptureRequest& request);
    CaptureBatchResult capture(const CaptureBatch& batch);

    engine::Camera& camera() { return camera_; }
    const engine::Camera& camera() const { return camera_; }

    engine::LightEnvironment& lightEnvironment() { return light_env_; }
    const engine::LightEnvironment& lightEnvironment() const { return light_env_; }

    RenderMode renderMode() const { return render_mode_; }
    void setRenderMode(RenderMode mode) { render_mode_ = mode; }

    SurfaceShading surfaceShading() const { return surface_shading_; }
    void setSurfaceShading(SurfaceShading shading) { surface_shading_ = shading; }

    void setHoveredPickId(uint32_t pickId);
    void clearHoveredPickId();

    bool showViewCube() const { return show_view_cube_; }
    void setShowViewCube(bool show) { show_view_cube_ = show; }
    bool hasHoveredViewCubeFace() const { return view_cube_interaction_.hasHoveredFace; }
    void clearViewCubeInteraction() { view_cube_interaction_ = {}; }

    const engine::ViewCubeLayout& viewCubeLayout() const { return view_cube_model_.layout(); }
    void setViewCubeLayout(const engine::ViewCubeLayout& layout);
    void setViewCubeSize(uint32_t size);
    void setViewCubeMargin(uint32_t margin);
    void setViewCubeCorner(engine::ViewCubeCorner corner);

    bool showOverlays() const { return show_overlays_; }
    void setShowOverlays(bool show) { show_overlays_ = show; }

    /// 按需烘焙 IBL（转发给 Renderer）。HDR 路径来自 ViewConfig::hdrPath。
    /// 通常由 DocumentSession 在 attachViewContext 时根据模型类型决定是否调用。
    /// 内部会再检查全局开关 iblEnabled()：关则不烘。
    void enableIBL();

    /// 全局 IBL 总开关（来自 ViewConfig::iblEnabled）。关闭时 enableIBL 不烘。
    bool iblEnabled() const { return ibl_enabled_; }

private:
    friend class CaptureService;

    ViewState buildViewState() const;
    void renderFrame(const ViewState& viewState);
    ViewState snapshotViewState() const;
    ViewState snapshotViewState(const engine::Camera& camera, const CaptureVisual& visual, uint32_t width,
                                uint32_t height) const;
    void onFrameEnd();
    bool handleViewCubeInput(const engine::InputEvent& event);
    void updateViewCubeHover(const engine::InputEvent& event);
    void setCameraToViewCubeFace(engine::ViewCubeFace face);

    bool isOffscreenSurface() const;
    uint32_t surfaceWidth() const;
    uint32_t surfaceHeight() const;
    bool readbackPixels(std::vector<uint8_t>& pixels);
    bool configureCaptureSurface(const engine::RenderCaptureDesc& desc, uint32_t width, uint32_t height);
    std::optional<RenderSurfaceDesc> captureSurfaceSnapshot() const;
    bool restoreCaptureSurface(const RenderSurfaceDesc& desc);

    RenderRuntimeHost runtime_host_;
    engine::Camera camera_{ engine::CameraMode::Trackball };

    std::unique_ptr<engine::Operator> default_op_;
    std::vector<std::unique_ptr<engine::Operator>> op_stack_;

    engine::LightEnvironment light_env_;
    RenderMode render_mode_ = RenderMode::ShadedWithEdges;
    SurfaceShading surface_shading_ = SurfaceShading::SolidLit;
    uint32_t hovered_pick_id_ = 0;
    bool has_hovered_pick_id_ = false;
    bool show_overlays_ = true;
    bool show_view_cube_ = true;
    bool consuming_view_cube_click_ = false;
    engine::ViewCubeInteractionState view_cube_interaction_;
    engine::ViewCubeModel view_cube_model_;

    int width_ = 800;
    int height_ = 600;

    // HDR 路径（由 ViewConfig::hdrPath 填充，enableIBL() 时使用）。
    // 是否实际启用由两层决定：(1) 全局开关 ibl_enabled_，(2) DocumentSession 按模型类型
    // 决定是否调用 enableIBL()——CAD 文档即使全局开关开了也不会调用。
    bool ibl_enabled_ = false;
    std::string hdr_path_ = "assets/envmap.hdr";
};

}  // namespace mulan::view
