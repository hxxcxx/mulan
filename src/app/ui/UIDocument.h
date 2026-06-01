/**
 * @file UIDocument.h
 * @brief UI 文档 — 桥接 World 数据层与 Viewport 渲染层
 * @author hxxcxx
 * @date 2026-04-22 (原始) / 2026-06-01 (重构)
 *
 * 职责：
 *  - 持有 World（数据源，由 DocumentManager::openFile 返回）
 *  - 绑定 Viewport，设置 World + 适配相机
 *  - 管理拾取映射（pickId → Entity::Id）
 *
 * 位于 QtApp 层，是唯一同时依赖 Document 模块（I/O）和 World 模块的类。
 */
#pragma once

#include <mulan/world/World.h>
#include <mulan/engine/math/Math.h>
#include <mulan/engine/math/AABB.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace mulan::world {
class Viewport;
}

class UIDocument {
public:
    /// 从已填充的 World 构造（由 DocumentManager::openFile 返回）
    explicit UIDocument(std::unique_ptr<mulan::world::World> world,
                        std::string displayName);
    ~UIDocument();

    // --- 数据层 ---

    mulan::world::World* world() { return m_world.get(); }
    const mulan::world::World* world() const { return m_world.get(); }

    const std::string& displayName() const { return m_displayName; }

    // --- 视图连接 ---

    /// 绑定 Viewport：设置 World + 适配相机
    void attachViewport(mulan::world::Viewport* viewport);

    /// 解除绑定
    void detachViewport();

    mulan::world::Viewport* viewport() const { return m_viewport; }

    /// 通过 pickId 反查 Entity::Id（拾取用）
    mulan::world::Entity::Id resolvePickId(uint32_t pickId) const;

private:
    std::unique_ptr<mulan::world::World> m_world;
    std::string m_displayName;

    mulan::world::Viewport* m_viewport = nullptr;

    /// pickId → Entity::Id 映射（拾取反查用）
    std::unordered_map<uint32_t, mulan::world::Entity::Id> m_pickIdMap;
};
