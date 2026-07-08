/**
 * @file asset_library.h
 * @brief AssetLibrary —— 文档层资产容器，管理可复用几何、材质与纹理数据
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "asset.h"
#include "curve_asset.h"
#include "face_asset.h"
#include "tessellated_asset.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "texture_asset.h"

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace mulan::asset {

class AssetLibrary {
public:
    AssetLibrary() = default;

    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;

    template <typename T, typename... Args>
    T* create(std::string name, Args&&... args) {
        static_assert(std::is_base_of_v<Asset, T>);
        AssetId id = allocateId();
        auto asset = std::make_unique<T>(id, std::move(name), std::forward<Args>(args)...);
        T* ptr = asset.get();
        assets_[id] = std::move(asset);
        return ptr;
    }

    Asset* asset(AssetId id);
    const Asset* asset(AssetId id) const;

    bool contains(AssetId id) const;
    void remove(AssetId id);
    void clear();

    size_t count() const { return assets_.size(); }
    size_t count(AssetKind kind) const;

private:
    AssetId allocateId();

    AssetIdValue next_id_ = 1;
    std::unordered_map<AssetId, std::unique_ptr<Asset>> assets_;
};

}  // namespace mulan::asset
