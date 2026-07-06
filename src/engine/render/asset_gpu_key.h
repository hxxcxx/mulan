/**
 * @file asset_gpu_key.h
 * @brief AssetGpuKey —— 资产派生的不可变 GPU 资源键。
 * @author hxxcxx
 * @date 2026-07-06
 */

#pragma once

#include <cstdint>
#include <functional>

namespace mulan::engine {

struct AssetGpuKey {
    uint64_t value = 0;

    constexpr explicit operator bool() const { return value != 0; }
    constexpr bool operator==(const AssetGpuKey&) const = default;
};

inline constexpr AssetGpuKey makeAssetGpuKey(uint64_t value) {
    return AssetGpuKey{ value };
}

}  // namespace mulan::engine

template <>
struct std::hash<mulan::engine::AssetGpuKey> {
    size_t operator()(mulan::engine::AssetGpuKey key) const noexcept { return std::hash<uint64_t>{}(key.value); }
};
