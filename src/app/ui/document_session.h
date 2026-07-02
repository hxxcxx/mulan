/**
 * @file document_session.h
 * @brief 表示一个已打开文档在界面中的运行会话。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include <mulan/document/document.h>
#include <mulan/render_scene/render_scene.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::view {
class ViewRuntime;
}

class DocumentSession {
public:
    explicit DocumentSession(std::unique_ptr<mulan::document::Document> doc);
    ~DocumentSession();

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    mulan::document::Document* document() { return document_.get(); }
    const mulan::document::Document* document() const { return document_.get(); }

    const std::string& displayName() const;

    void syncRenderScene();

    mulan::render_scene::RenderScene& renderScene() { return render_scene_; }
    const mulan::render_scene::RenderScene& renderScene() const { return render_scene_; }

    void attachViewRuntime(mulan::view::ViewRuntime* runtime);
    void detachViewRuntime();

    mulan::view::ViewRuntime* viewRuntime() const { return view_runtime_; }

    mulan::scene::EntityId resolvePickId(uint32_t pickId) const;

private:
    std::unique_ptr<mulan::document::Document> document_;
    mulan::render_scene::RenderScene render_scene_;
    mulan::view::ViewRuntime* view_runtime_ = nullptr;
    std::unordered_map<uint32_t, mulan::scene::EntityId> pick_id_map_;
};
