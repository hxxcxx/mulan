/**
 * @file ui_document.h
 * @brief UI 文档 — Document 的视图层包装，桥接 Document 与 Viewport 渲染
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-30 (持有 Document)
 *
 * 职责：
 *  - 持有 Document（真实数据源 + World）
 *  - 绑定 Viewport，把 Document 的 World 接入渲染
 *  - 管理拾取映射（pickId → Entity::Id）
 *
 * 位于 QtApp 层。真实几何（B-Rep）由 Document 层持有，本类不感知 OCCT。
 */
#pragma once

#include <mulan/document/document.h>
#include <mulan/world/entity.h>
#include <mulan/engine/math/math.h>
#include <mulan/engine/math/aabb.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::world {
class Viewport;
}

class UIDocument {
public:
    /// 从已填充的 Document 构造（由 FileManager::openFile 返回）
    explicit UIDocument(std::unique_ptr<mulan::document::Document> doc);
    ~UIDocument();

    // --- 数据层 ---

    mulan::document::Document* document() { return document_.get(); }
    const mulan::document::Document* document() const { return document_.get(); }

    /// 便捷访问 Document 拥有的 World
    mulan::world::World* world();
    const mulan::world::World* world() const;

    const std::string& displayName() const;

    // --- 视图连接 ---

    /// 绑定 Viewport：设置 World + 适配相机
    void attachViewport(mulan::world::Viewport* viewport);

    /// 解除绑定
    void detachViewport();

    mulan::world::Viewport* viewport() const { return viewport_; }

    /// 通过 pickId 反查 Entity::Id（拾取用）
    mulan::world::Entity::Id resolvePickId(uint32_t pickId) const;

private:
    std::unique_ptr<mulan::document::Document> document_;
    mulan::world::Viewport* viewport_ = nullptr;

    /// pickId → Entity::Id 映射（拾取反查用）
    std::unordered_map<uint32_t, mulan::world::Entity::Id> pick_id_map_;
};
