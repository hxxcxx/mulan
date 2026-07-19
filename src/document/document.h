/**
 * @file document.h
 * @brief Document 是导入模型、资源库和场景实例的数据入口。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include <cstddef>
#include <mulan/asset/asset_id.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/modeling/core/shape.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <vector>

namespace mulan::scene {
class Scene;
}

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan {

class Document {
public:
    explicit Document(std::string displayName);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    scene::EntityId addBody(modeling::Shape shape, std::string name);
    scene::EntityId addFace(std::string name, asset::FaceDefinition face);
    scene::EntityId addMesh(std::string name, std::vector<asset::MeshPrimitive> primitives);

    scene::EntityId addSceneInstance(std::string name, asset::AssetId geometry,
                                     std::vector<asset::AssetId> materialSlots = {});
    /// 确认受控几何资产 mutator 已完成，并把文档标为已修改。
    /// 渲染 bounds 由 GeometryAsset + 实体 world transform 唯一派生，不在 Scene 重复缓存。
    bool markGeometryChanged(scene::EntityId entity);
    /// 返回当前场景中直接引用指定几何资产的实体数量。
    size_t geometryReferenceCount(asset::AssetId geometry) const;
    /// 仅在没有场景实体引用时删除几何资产。
    bool removeGeometryAssetIfUnreferenced(asset::AssetId geometry);
    bool removeEntity(scene::EntityId entity, bool removeGeometryAsset = true);

    scene::Scene* scene() { return scene_.get(); }
    const scene::Scene* scene() const { return scene_.get(); }

    asset::AssetLibrary* assets() { return assets_.get(); }
    const asset::AssetLibrary* assets() const { return assets_.get(); }

    const std::string& displayName() const { return display_name_; }
    const std::string& filePath() const { return file_path_; }
    void setFilePath(std::string path) { file_path_ = std::move(path); }

    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }

private:
    std::unique_ptr<scene::Scene> scene_;
    std::unique_ptr<asset::AssetLibrary> assets_;
    std::string display_name_;
    std::string file_path_;
    bool dirty_ = false;
};

}  // namespace mulan
