/**
 * @file document_session.h
 * @brief 表示一个已打开文档在界面中的运行会话。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include <mulan/io/document.h>
#include <mulan/view/render_scene.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::view {
class ViewContext;
}

class DocumentSession {
public:
    explicit DocumentSession(std::unique_ptr<mulan::io::Document> doc);
    ~DocumentSession();

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    mulan::io::Document* document() { return document_.get(); }
    const mulan::io::Document* document() const { return document_.get(); }

    const std::string& displayName() const;

    void syncRenderScene();

    /// 把 Scene 的变更传播到 RenderScene 并触发一帧重绘。
    /// 在任何修改 Document 内容（如 scene()->setVisible/setGeometry/...）之后调用。
    /// 这是接通"文档变更 → 视图更新"链路的标准入口：Scene 的 EntityDirty 标记
    /// 在此被消费（当前为全量 resync，未来可改为增量）。
    void requestRefresh();

    mulan::render_scene::RenderScene& renderScene() { return render_scene_; }
    const mulan::render_scene::RenderScene& renderScene() const { return render_scene_; }

    void attachViewContext(mulan::view::ViewContext* runtime);
    void detachViewContext();

    mulan::view::ViewContext* viewContext() const { return view_context_; }

    mulan::scene::EntityId resolvePickId(uint32_t pickId) const;

private:
    std::unique_ptr<mulan::io::Document> document_;
    mulan::render_scene::RenderScene render_scene_;
    mulan::view::ViewContext* view_context_ = nullptr;
    std::unordered_map<uint32_t, mulan::scene::EntityId> pick_id_map_;
};
