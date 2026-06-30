/**
 * @file Document.h
 * @brief Document — 真实数据源层，持有 B-Rep + 拥有 World
 * @author hxxcxx
 * @date 2026-06-30
 *
 * 设计定位：
 *  - Document 是"真理之源"（source of truth），持有真实几何数据（B-Rep TopoDS_Shape）。
 *  - World 是它的"渲染场景"视图：Document 通过 addSolid 创建 Entity + 挂渲染几何。
 *  - World 不持有 B-Rep，不知道 OCCT 存在（OCCT 只在 Document 层）。
 *  - 未来建模/编辑/Save 都以 Document 为落点。
 *
 * 数据流：
 *   STEP 文件 → Importer → document.addSolid(TopoDS_Shape)
 *                              ↓ Document 内部
 *                         存 TopoDS_Shape（在 SolidGeometryData 里）
 *                         + world.createEntity + entity.setGeometry(SolidGeometryData)
 *                              ↓ 渲染时惰性三角化
 *                         World / SceneProxy / GPU
 */
#pragma once

#include "DocumentExport.h"

#include <memory>
#include <string>

// 前向声明，避免在头文件暴露 OCCT / World 细节
class TopoDS_Shape;

namespace mulan::world {
class World;
class Entity;
}

namespace mulan::document {

class DOCUMENT_API Document {
public:
    explicit Document(std::string displayName);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // ---------- 真实数据（B-Rep 源）----------

    /// 添加一个 Solid：存 TopoDS_Shape + 创建 Entity + 挂渲染几何。
    /// 由 OCCTImporter 调用。返回创建的 Entity（非拥有）。
    world::Entity* addSolid(const TopoDS_Shape& shape, std::string name);

    // ---------- 渲染场景（拥有）----------

    /// 拥有的 World（渲染场景）。Viewport / UIDocument 借用。
    world::World* world() { return m_world.get(); }
    const world::World* world() const { return m_world.get(); }

    // ---------- 元数据 ----------

    const std::string& displayName() const { return m_displayName; }
    const std::string& filePath() const { return m_filePath; }
    void setFilePath(std::string path) { m_filePath = std::move(path); }

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

private:
    std::unique_ptr<world::World> m_world;
    // B-Rep 源目前由各 Entity 的 SolidGeometryData 持有（内含 TopoDS_Shape）。
    // 未来如需集中管理 B-Rep 源（脱离 Entity，支持显式拓扑查询/编辑），
    // 可在此增加 m_brepSources 等成员。
    std::string m_displayName;
    std::string m_filePath;
    bool m_dirty = false;
};

} // namespace mulan::document
