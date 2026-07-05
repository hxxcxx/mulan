/**
 * @file document_view_binding.h
 * @brief 将 DocumentSession 绑定到 ViewContext，并隐藏内部渲染缓存。
 *
 * @author hxxcxx
 * @date 2026-07-05
 */
#pragma once

#include <mulan/scene/entity_id.h>

#include <cstdint>
#include <memory>

class DocumentSession;

namespace mulan::view {
class ViewContext;
}

class DocumentViewBinding {
public:
    DocumentViewBinding() = default;
    ~DocumentViewBinding();

    DocumentViewBinding(const DocumentViewBinding&) = delete;
    DocumentViewBinding& operator=(const DocumentViewBinding&) = delete;

    void bind(DocumentSession& session, mulan::view::ViewContext& view);
    void unbind();
    bool isBound() const { return session_ && view_; }

    void refresh();
    void fitAll();

    mulan::scene::EntityId resolvePickId(uint32_t pickId) const;

private:
    struct RenderCache;

    void syncRenderCache();
    void rebuildPickIdMap();
    void applyViewPreferences();
    void injectRenderCache();

    DocumentSession* session_ = nullptr;
    mulan::view::ViewContext* view_ = nullptr;
    std::unique_ptr<RenderCache> render_cache_;
};
