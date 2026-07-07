/**
 * @file asset.h
 * @brief Asset —— 文档层可复用数据的资产基类
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "asset_id.h"

#include <cstdint>
#include <string>
#include <utility>

namespace mulan::asset {

enum class AssetKind : uint8_t {
    Unknown = 0,
    Geometry,
    Curve,
    Mesh,
    Tessellated,
    Material,
    Texture,
};

class Asset {
public:
    Asset(AssetId id, AssetKind kind, std::string name);
    virtual ~Asset() = default;

    AssetId id() const { return id_; }
    AssetKind kind() const { return kind_; }

    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

private:
    AssetId id_;
    AssetKind kind_ = AssetKind::Unknown;
    std::string name_;
};

}  // namespace mulan::asset
