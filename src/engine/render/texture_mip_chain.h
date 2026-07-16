/**
 * @file texture_mip_chain.h
 * @brief 为 RGBA8 资产纹理生成确定性的 CPU Mip 链。
 * @author hxxcxx
 * @date 2026-07-16
 *
 * 该模块不接触 RHI。sRGB 纹理在降采样前转到线性空间，alpha 始终线性处理；
 * 任意奇数尺寸使用面积权重，避免简单 2x2 采样丢失最后一行或最后一列。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mulan::engine {

struct Rgba8MipLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::byte> pixels;
};

/// 返回包含 level 0 的完整链；输入尺寸/字节数非法时返回空链。
std::vector<Rgba8MipLevel> buildRgba8MipChain(std::span<const std::byte> levelZero, uint32_t width, uint32_t height,
                                              bool srgb);

}  // namespace mulan::engine
