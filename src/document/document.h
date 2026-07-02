/**
 * @file document.h
 * @brief Document 是导入模型、资源库和场景实例的真实数据入口
 * @author hxxcxx
 * @date 2026-07-03
 */

#pragma once

#include "document_export.h"

#include <cstddef>
#include <mulan/asset/asset_id.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <string>
#include <vector>

class TopoDS_Shape;

namespace mulan::scene {
class Scene;
}

namespace mulan::asset {
class AssetLibrary;
}

namespace mulan::document {

struct DocumentSceneMirrorStats {
    size_t sceneEntityCount = 0;
    size_t assetCount = 0;
    size_t brepAssetCount = 0;

    bool consistent() const {
        return sceneEntityCount == brepAssetCount;
    }
};

class DOCUMENT_API Document {
public:
    explicit Document(std::string displayName);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    /// 添加一个形体资源，并创建对应的场景实例。
    scene::EntityId addSolid(const TopoDS_Shape& shape, std::string name);

    /// 在 Scene/Asset 结构中添加一个场景实例。
    scene::EntityId addSceneInstance(std::string name,
                                     asset::AssetId geometry,
                                     std::vector<asset::AssetId> materialSlots = {});

    scene::Scene* scene() { return scene_.get(); }
    const scene::Scene* scene() const { return scene_.get(); }

    asset::AssetLibrary* assets() { return assets_.get(); }
    const asset::AssetLibrary* assets() const { return assets_.get(); }

    DocumentSceneMirrorStats sceneMirrorStats() const;
    bool validateSceneMirror() const { return sceneMirrorStats().consistent(); }

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

} // namespace mulan::document
