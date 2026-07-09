/**
 * @file document_render_binding.h
 * @brief DocumentRenderBinding 负责文档到视图渲染缓存的同步与注入。
 *
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "document_render_cache.h"

#include <mulan/engine/render/light_environment.h>

#include <span>

class DocumentSession;

namespace mulan::view {
class ViewContext;
}

namespace mulan::app {

class DocumentRenderBinding {
public:
    DocumentRenderBinding() = default;
    ~DocumentRenderBinding();

    DocumentRenderBinding(const DocumentRenderBinding&) = delete;
    DocumentRenderBinding& operator=(const DocumentRenderBinding&) = delete;

    void bind(DocumentSession& session, view::ViewContext& view);
    void unbind();
    bool isBound() const { return session_ && view_; }

    void refresh();
    void fitAll();
    void syncRenderCache();
    void injectRenderCache();
    void fitCameraClipPlanesToSceneBounds();

    DocumentSession* session() const { return session_; }
    view::ViewContext* view() const { return view_; }
    const view::RenderScene* renderScene() const;

private:
    void applyViewPreferences();

    DocumentSession* session_ = nullptr;
    view::ViewContext* view_ = nullptr;
    DocumentRenderCache render_cache_;
};

}  // namespace mulan::app
