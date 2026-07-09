/**
 * @file document.h
 * @brief Document 是导入模型、资源库和场景实例的数据入口。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include "io_export.h"

#include <cstddef>
#include <mulan/asset/asset_id.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/math/math.h>
#include <mulan/modeling_core/shape.h>
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

namespace mulan::io {

class IO_API Document {
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
    bool markGeometryChanged(scene::EntityId entity, const math::AABB3& bounds);
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

}  // namespace mulan::io
