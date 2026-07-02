/**
 * @file document_session.h
 * @brief DocumentSession 表示一个已打开文档在界面中的运行会话
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include <mulan/document/document.h>
#include <mulan/engine/math/aabb.h>
#include <mulan/engine/math/math.h>
#include <mulan/render_scene/render_scene.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::view {
class Viewport;
}

class DocumentSession {
public:
    /// 从已经填充的 Document 构造一个界面会话。
    explicit DocumentSession(std::unique_ptr<mulan::document::Document> doc);
    ~DocumentSession();

    mulan::document::Document* document() { return document_.get(); }
    const mulan::document::Document* document() const { return document_.get(); }

    const std::string& displayName() const;

    /// 同步从 Document 派生出的渲染场景缓存。
    void syncRenderScene();

    /// 当前会话持有的渲染场景缓存。
    mulan::render_scene::RenderScene& renderScene() { return render_scene_; }
    const mulan::render_scene::RenderScene& renderScene() const { return render_scene_; }

    /// 绑定旧 Viewport，当前只作为兼容显示入口。
    void attachViewport(mulan::view::Viewport* viewport);

    /// 解除绑定。
    void detachViewport();

    mulan::view::Viewport* viewport() const { return viewport_; }

    /// 通过 pickId 反查场景实体。
    mulan::scene::EntityId resolvePickId(uint32_t pickId) const;

private:
    std::unique_ptr<mulan::document::Document> document_;
    mulan::render_scene::RenderScene render_scene_;
    mulan::view::Viewport* viewport_ = nullptr;

    /// pickId 到场景实体的映射。
    std::unordered_map<uint32_t, mulan::scene::EntityId> pick_id_map_;
};
