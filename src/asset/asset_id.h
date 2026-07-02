/**
 * @file asset_id.h
 * @brief AssetId —— 文档资产库使用的稳定资产标识
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <cstdint>
#include <functional>

namespace mulan::asset {

using AssetIdValue = uint64_t;

struct AssetId {
    AssetIdValue value = 0;

    static constexpr AssetId invalid() { return {}; }

    constexpr bool valid() const { return value != 0; }
    constexpr explicit operator bool() const { return valid(); }

    friend constexpr bool operator==(AssetId a, AssetId b) {
        return a.value == b.value;
    }

    friend constexpr bool operator!=(AssetId a, AssetId b) {
        return !(a == b);
    }
};

} // namespace mulan::asset

template<>
struct std::hash<mulan::asset::AssetId> {
    size_t operator()(mulan::asset::AssetId id) const noexcept {
        return std::hash<mulan::asset::AssetIdValue>{}(id.value);
    }
};
