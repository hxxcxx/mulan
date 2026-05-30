/**
 * @file Document.h
 * @brief 数据容器 — 实体的存储与管理，类似数据库
 * @author hxxcxx
 * @date 2026-04-22
 *
 * Document 是所有 Entity 的拥有者。
 * 外部系统通过 EntityHandle（id + doc 指针）安全引用实体，
 * 不持有裸指针。
 */
#pragma once

#include "DocumentExport.h"
#include "EntityId.h"
#include "Entity.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::document {

class DocumentManager;

class DOCUMENT_API Document {
public:
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    ~Document() = default;

    /// 工厂方法 — 只有 DocumentManager 和 Importer 能创建
    static std::unique_ptr<Document> create();

    // --- Entity CRUD ---

    /// 创建实体并附带几何数据，返回 ID
    EntityId createEntity(std::string name, std::unique_ptr<Geometry> geometry);

    /// 创建空实体（无几何，用于分组/装配体），返回 ID
    EntityId createEntity(std::string name = {});

    /// 按 ID 查找实体，不存在返回 nullptr
    Entity* findEntity(EntityId id);
    const Entity* findEntity(EntityId id) const;

    /// 按 ID 删除实体（同时清除子实体的 parentId 引用）
    /// @return true 表示成功删除
    bool removeEntity(EntityId id);

    /// 实体总数
    size_t entityCount() const;

    // --- 层级查询 ---

    /// 获取所有根实体 ID（没有父节点的实体）
    std::vector<EntityId> rootEntityIds() const;

    /// 获取指定实体的所有直接子实体 ID
    std::vector<EntityId> childEntityIds(EntityId parentId) const;

    // --- 遍历 ---

    /// 遍历所有实体（顺序不保证）
    using EntityCallback = std::function<void(Entity&)>;
    using ConstEntityCallback = std::function<void(const Entity&)>;
    void forEachEntity(const EntityCallback& cb);
    void forEachEntity(const ConstEntityCallback& cb) const;

    // --- 文档属性 ---

    const std::string& filePath() const { return m_filePath; }
    void setFilePath(std::string path) { m_filePath = std::move(path); }

    const std::string& displayName() const { return m_displayName; }
    void setDisplayName(std::string name) { m_displayName = std::move(name); }

    bool isModified() const { return m_modified; }
    void setModified(bool m) { m_modified = m; }

    /// 统计摘要（用于状态栏显示）
    std::string summary() const;

    /// 清空所有实体
    void clear();

private:
    Document() = default;

    friend class DocumentManager;

    std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
    std::string m_filePath;
    std::string m_displayName;
    bool m_modified = false;
};

} // namespace mulan::document
