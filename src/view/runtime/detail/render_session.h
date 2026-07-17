/**
 * @file render_session.h
 * @brief RenderSession 是 ViewContext 唯一依赖的视图级渲染会话。
 * @author hxxcxx
 * @date 2026-07-15
 *
 * 会话在调用线程构建自持有 RenderSubmission，并将其交给 GPU 执行域。
 * Device、Surface、渲染线程和 engine renderer 均属于内部实现，不向 view 模块外暴露。
 * RenderSession 由创建它的调用线程独占；只有 RenderWorker 内部跨越线程边界。
 */

#pragma once

#include "render_surface_state.h"

#include "scene_sync/render_submission_builder.h"

#include <mulan/core/result/error.h>
#include <mulan/render/frontend/render_capture.h>
#include <mulan/view/core/view_config.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::view {

class PreviewLayer;
class RenderScene;

namespace detail {
class RenderWorker;

class RenderSession {
public:
    RenderSession();
    ~RenderSession();

    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;

    ResultVoid initWindow(const ViewConfig& config, int width, int height);
    void shutdown();

    bool isInitialized() const;
    /// owner 线程主动 drain ACK/Failure；失败时同步销毁执行域并使 builder 资源失效。
    ResultVoid pollRuntime();

    void setRenderScene(const RenderScene* scene, const asset::AssetLibrary* assets);
    void setPreviewLayer(const PreviewLayer* preview);
    void setLightEnvironment(const engine::LightEnvironment& lightEnvironment);

    void submitFrame(const ViewState& viewState);
    Result<engine::RenderCaptureResult> capture(const ViewState& viewState, const engine::RenderCaptureDesc& desc);
    RenderSurfaceState resize(int width, int height);
    void enableIBL(const std::string& hdrPath);

    RenderSurfaceState surfaceState() const;

private:
    void assertOwnerThread() const;
    ResultVoid drainWorkerEvents();
    void failExecution(const Error& error);
    void discardExecutionDomain();
    void clearAssetResources();

    RenderSubmissionBuilder submission_builder_;
    std::unique_ptr<RenderWorker> worker_;
    const asset::AssetLibrary* asset_source_ = nullptr;
    std::optional<Error> last_runtime_failure_;
    std::thread::id owner_thread_;
};

}  // namespace detail
}  // namespace mulan::view
