/**
 * @file view_context.h
 * @brief ViewContext 管理视图状态、相机和交互，并生成 ViewState。
 * @author hxxcxx
 */
#pragma once

#include <mulan/view/core/view_config.h>
#include <mulan/view/core/view_state.h>
#include <mulan/view/core/preview_layer.h>

#include <mulan/view/capture/capture_batch.h>

#include "mulan/core/result/error.h"
#include "mulan/interaction/camera_manipulator.h"
#include "mulan/interaction/input_event.h"
#include "mulan/interaction/operator.h"
#include "mulan/render/light_environment.h"
#include "mulan/render/camera/camera.h"
#include "mulan/render/overlay/view_cube_model.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {
class RenderScene;
namespace detail {
class RenderSession;
}
}  // namespace mulan::view

namespace mulan::view {

class ViewContext {
public:
    ViewContext();
    ~ViewContext();

    ViewContext(const ViewContext&) = delete;
    ViewContext& operator=(const ViewContext&) = delete;

    bool init(const ViewConfig& config, int width, int height);
    bool initOffscreen(const ViewConfig& config, int width, int height);
    bool initOffscreen(int width, int height);
    void shutdown();

    bool isInitialized() const;
    /// 专用渲染线程的异步失败快照；无失败时返回空。
    std::optional<core::Error> runtimeFailure() const;
    /// owner 主动回收渲染线程 ACK/Failure，并在失败时完成执行域清理。
    core::Result<void> pollRuntime();

    void setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets);

    void renderFrame();
    void resize(int width, int height);

    /// 兼容入口：仅返回是否处理。需要判断导航/覆盖层/模态语义时使用 dispatchInput。
    bool handleInput(const engine::InputEvent& event);
    engine::InputOutcome dispatchInput(const engine::InputEvent& event);

    void pushOperator(std::unique_ptr<engine::Operator> op);
    void popOperator();
    /// 精确移除指定 Operator；用于外部所有者撤销自己安装的模态交互。
    bool removeOperator(const engine::Operator* op);

    engine::Operator* activeOperator() const;
    engine::Operator* defaultOperator() const { return default_op_.get(); }

    core::Result<engine::RenderCaptureResult> capture(const engine::RenderCaptureDesc& desc);
    core::Result<CaptureImage> capture(const CaptureRequest& request);
    CaptureBatchResult capture(const CaptureBatch& batch);

    engine::Camera& camera() { return camera_; }
    const engine::Camera& camera() const { return camera_; }

    engine::LightEnvironment& lightEnvironment() { return light_env_; }
    const engine::LightEnvironment& lightEnvironment() const { return light_env_; }
    void setSceneLights(std::span<const engine::Light> lights);

    PreviewLayer& previewLayer() { return preview_layer_; }
    const PreviewLayer& previewLayer() const { return preview_layer_; }
    void clearPreview();

    RenderMode renderMode() const { return render_mode_; }
    void setRenderMode(RenderMode mode) { render_mode_ = mode; }

    SurfaceShading surfaceShading() const { return surface_shading_; }
    void setSurfaceShading(SurfaceShading shading) { surface_shading_ = shading; }

    void setHoveredPickId(engine::PickId pickId);
    void clearHoveredPickId();
    void setSelectionVisualState(engine::SelectionVisualState state);
    void clearSelectionVisualState();

    bool showViewCube() const { return show_view_cube_; }
    void setShowViewCube(bool show) { show_view_cube_ = show; }
    bool hasHoveredViewCubeFace() const { return view_cube_interaction_.hasHoveredPart; }
    void clearViewCubeInteraction() {
        consuming_view_cube_click_ = false;
        view_cube_interaction_ = {};
    }

    const engine::ViewCubeLayout& viewCubeLayout() const { return view_cube_model_.layout(); }
    void setViewCubeLayout(const engine::ViewCubeLayout& layout);
    void setViewCubeSize(uint32_t size);
    void setViewCubeMargin(uint32_t margin);
    void setViewCubeCorner(engine::ViewCubeCorner corner);
    /// 恢复文档视图的确定性默认相机，并保留当前视口尺寸。
    void resetCamera();
    void setCameraToWorldXY();

    bool showOverlays() const { return show_overlays_; }
    void setShowOverlays(bool show) { show_overlays_ = show; }
    /// 按需烘焙 IBL。HDR 路径来自 ViewConfig::hdrPath。
    /// 通常由上层会话根据模型类型决定是否调用。
    /// 内部会再检查全局开关 iblEnabled()：关则不烘。
    void enableIBL();

    /// 全局 IBL 总开关（来自 ViewConfig::iblEnabled）。关闭时 enableIBL 不烘。
    bool iblEnabled() const { return ibl_enabled_; }

private:
    friend class CaptureService;

    ViewState buildViewState() const;
    void renderFrame(const ViewState& viewState);
    ViewState snapshotViewState(uint32_t width, uint32_t height) const;
    ViewState snapshotViewState(const engine::Camera& camera, const CaptureVisual& visual, uint32_t width,
                                uint32_t height) const;
    bool handleViewCubeInput(const engine::InputEvent& event);
    void updateViewCubeHover(const engine::InputEvent& event);
    void setCameraToViewCubePart(const engine::ViewCubePart& part);

    uint32_t surfaceWidth() const;
    uint32_t surfaceHeight() const;
    core::Result<engine::RenderCaptureResult> captureFrame(const ViewState& viewState,
                                                           const engine::RenderCaptureDesc& desc);

    std::unique_ptr<detail::RenderSession> render_session_;
    engine::Camera camera_{ engine::CameraMode::Trackball };

    std::unique_ptr<engine::Operator> default_op_;
    std::vector<std::unique_ptr<engine::Operator>> op_stack_;

    engine::LightEnvironment light_env_;
    PreviewLayer preview_layer_;
    RenderMode render_mode_ = RenderMode::ShadedWithEdges;
    SurfaceShading surface_shading_ = SurfaceShading::SolidLit;
    engine::PickId hovered_pick_id_;
    engine::SelectionVisualState selection_visual_state_;
    bool show_overlays_ = true;
    bool show_view_cube_ = true;
    bool consuming_view_cube_click_ = false;
    engine::ViewCubeInteractionState view_cube_interaction_;
    engine::ViewCubeModel view_cube_model_;

    int width_ = 800;
    int height_ = 600;

    // HDR 路径（由 ViewConfig::hdrPath 填充，enableIBL() 时使用）。
    // 是否实际启用由两层决定：(1) 全局开关 ibl_enabled_，(2) 上层会话按模型类型
    // 决定是否调用 enableIBL()——CAD 文档即使全局开关开了也不会调用。
    bool ibl_enabled_ = false;
    std::string hdr_path_ = "assets/envmap.hdr";
};

}  // namespace mulan::view
