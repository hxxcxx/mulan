/**
 * @file document_render_binding.h
 * @brief DocumentRenderBinding 负责文档到视图渲染缓存的同步与注入。
 *
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_change.h"
#include "document_render_cache.h"

#include <mulan/render/camera/camera.h>
#include <mulan/render/light_environment.h>

#include <cstdint>
#include <functional>
#include <span>

class DocumentSession;

namespace mulan::view {
class ViewContext;
}

namespace mulan::editor {

enum class ClipUpdateMode : uint8_t {
    Settled,
    Interactive,
};

class DocumentRenderBinding {
public:
    using FrameInvalidationCallback = std::function<void()>;

    DocumentRenderBinding() = default;
    ~DocumentRenderBinding();

    DocumentRenderBinding(const DocumentRenderBinding&) = delete;
    DocumentRenderBinding& operator=(const DocumentRenderBinding&) = delete;

    void bind(DocumentSession& session, view::ViewContext& view);
    void unbind();
    bool isBound() const { return session_ && view_; }

    /// 注入上层帧失效入口。文档层只报告“需要新帧”，不直接执行渲染。
    void setFrameInvalidationCallback(FrameInvalidationCallback callback);

    void refresh();
    /// 仅同步选择、高亮等渲染状态，不使场景包围范围失效。
    void refreshVisualState();
    void fitAll();
    /// 在构建帧快照前，按场景与相机深度版本统一更新裁剪面。
    void prepareFrame(ClipUpdateMode mode = ClipUpdateMode::Settled);
    DocumentSession* session() const { return session_; }
    view::ViewContext* view() const { return view_; }
    const view::RenderScene* renderScene() const;

private:
    void syncRenderCache();
    void injectRenderCache();
    math::Sphere3 cameraBoundsSphere() const;
    void applyViewPreferences();
    void invalidateFrame() const;
    void handleDocumentChange(const DocumentChangeStamp& change);

    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    DocumentRenderCache render_cache_;
    FrameInvalidationCallback frame_invalidation_callback_;
    uint64_t prepared_camera_depth_revision_ = 0;
    uint64_t prepared_preview_generation_ = 0;
    bool scene_bounds_dirty_ = false;
    bool clip_tightening_pending_ = false;
    uint64_t change_subscription_ = 0;
};

}  // namespace mulan::editor
