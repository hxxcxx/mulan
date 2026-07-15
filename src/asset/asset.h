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

using AssetRevision = uint64_t;

enum class AssetKind : uint8_t {
    Unknown = 0,
    Geometry,
    Curve,
    Face,
    Mesh,
    Tessellated,
    BRep,
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
    void setName(std::string name);

    /// 资产内容版本。初始有效资产为 1，仅真实内容变化时单调递增。
    AssetRevision revision() const noexcept { return revision_; }

protected:
    /// 由派生类的受控 mutator 调用；在版本耗尽时拒绝静默回绕。
    void touch();

    template <typename T>
    bool assignIfChanged(T& target, T value) {
        if (target == value)
            return false;
        touch();
        target = std::move(value);
        return true;
    }

private:
    AssetId id_;
    AssetKind kind_ = AssetKind::Unknown;
    std::string name_;
    AssetRevision revision_ = 1;
};

}  // namespace mulan::asset
