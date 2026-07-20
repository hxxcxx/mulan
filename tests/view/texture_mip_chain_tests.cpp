/**
 * @file texture_mip_chain_tests.cpp
 * @brief 验证 RGBA8 Mip 链尺寸、奇数边界和 sRGB 线性滤波。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include "../../src/engine/render/texture_mip_chain.h"

#include <gtest/gtest.h>

#include <array>

namespace mulan::engine {
namespace {

uint8_t channel(const Rgba8MipLevel& level, size_t pixel, size_t component) {
    return std::to_integer<uint8_t>(level.pixels[pixel * 4u + component]);
}

TEST(TextureMipChainTests, BuildsCompleteChainAndKeepsOddImageCoverage) {
    std::array<std::byte, 3u * 5u * 4u> pixels{};
    for (size_t i = 0; i < 15; ++i)
        pixels[i * 4u + 3u] = std::byte{ 255 };
    pixels[(15u - 1u) * 4u] = std::byte{ 255 };

    const auto chain = buildRgba8MipChain(pixels, 3, 5, false);
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(std::pair(chain[0].width, chain[0].height), std::pair(3u, 5u));
    EXPECT_EQ(std::pair(chain[1].width, chain[1].height), std::pair(1u, 2u));
    EXPECT_EQ(std::pair(chain[2].width, chain[2].height), std::pair(1u, 1u));
    EXPECT_GT(channel(chain.back(), 0, 0), 0u);
    EXPECT_EQ(channel(chain.back(), 0, 3), 255u);
}

TEST(TextureMipChainTests, FiltersSrgbColorInLinearSpaceButAlphaLinearly) {
    const std::array<uint8_t, 16> raw{ 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 255 };
    const auto bytes = std::as_bytes(std::span(raw));
    const auto linear = buildRgba8MipChain(bytes, 2, 2, false);
    const auto srgb = buildRgba8MipChain(bytes, 2, 2, true);
    ASSERT_EQ(linear.size(), 2u);
    ASSERT_EQ(srgb.size(), 2u);
    EXPECT_NEAR(channel(linear[1], 0, 0), 128, 1);
    EXPECT_NEAR(channel(srgb[1], 0, 0), 188, 1);
    EXPECT_NEAR(channel(srgb[1], 0, 3), 128, 1);
}

TEST(TextureMipChainTests, RejectsMismatchedInputWithoutPartialResult) {
    const std::array<std::byte, 3> invalid{};
    EXPECT_TRUE(buildRgba8MipChain(invalid, 1, 1, false).empty());
    EXPECT_TRUE(buildRgba8MipChain({}, 0, 1, false).empty());
}

}  // namespace
}  // namespace mulan::engine
