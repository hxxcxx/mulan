/**
 * @file document.h
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

#include "document_export.h"

#include <cstddef>
#include <mulan/asset/asset_id.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <vector>

// 前向声明，避免在头文件暴露 OCCT / World 细节
class TopoDS_Shape;

namespace mulan::world {
class World;
class Entity;
}

namespace mulan::scene {
class Scene;
}

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::document {

struct DocumentSceneMirrorStats {
    size_t worldEntityCount = 0;
    size_t sceneEntityCount = 0;
    size_t assetCount = 0;
    size_t brepAssetCount = 0;

    bool consistent() const {
        return worldEntityCount == sceneEntityCount
            && sceneEntityCount == brepAssetCount;
    }
};

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

    /// 在新 Scene/Asset 架构中添加一个场景实例。
    /// 当前仅作为迁移入口：旧 world 仍负责实际渲染。
    scene::EntityId addSceneInstance(std::string name,
                                     asset::AssetId geometry,
                                     std::vector<asset::AssetId> materialSlots = {});

    // ---------- 渲染场景（拥有）----------

    /// 旧显示链路使用的 World，当前由 Viewport / DocumentSession 过渡借用。
    world::World* world() { return world_.get(); }
    const world::World* world() const { return world_.get(); }

    /// New editor-ready scene model. It is introduced beside the legacy World
    /// and is not wired into rendering yet.
    scene::Scene* scene() { return scene_.get(); }
    const scene::Scene* scene() const { return scene_.get(); }

    /// Reusable document assets. Serialization and editing are reserved for
    /// later phases.
    asset::AssetLibrary* assets() { return assets_.get(); }
    const asset::AssetLibrary* assets() const { return assets_.get(); }

    /// 检查旧 world 与新 Scene/Asset 迁移镜像是否一致。
    DocumentSceneMirrorStats sceneMirrorStats() const;
    bool validateSceneMirror() const { return sceneMirrorStats().consistent(); }

    // ---------- 元数据 ----------

    const std::string& displayName() const { return display_name_; }
    const std::string& filePath() const { return file_path_; }
    void setFilePath(std::string path) { file_path_ = std::move(path); }

    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }

private:
    std::unique_ptr<world::World> world_;
    std::unique_ptr<scene::Scene> scene_;
    std::unique_ptr<asset::AssetLibrary> assets_;
    // B-Rep 源目前由各 Entity 的 SolidGeometryData 持有（内含 TopoDS_Shape）。
    // 未来如需集中管理 B-Rep 源（脱离 Entity，支持显式拓扑查询/编辑），
    // 可在此增加 brep_sources_ 等成员。
    std::string display_name_;
    std::string file_path_;
    bool dirty_ = false;
};

} // namespace mulan::document
