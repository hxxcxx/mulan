/**
 * @file texture_mip_chain.cpp
 * @brief RGBA8 面积滤波与 sRGB 正确降采样实现。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include "texture_mip_chain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mulan::engine {
namespace {

float srgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

uint8_t encode(float value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0f), 0l, 255l));
}

Rgba8MipLevel downsample(const Rgba8MipLevel& source, bool srgb) {
    Rgba8MipLevel result;
    result.width = std::max(1u, source.width / 2u);
    result.height = std::max(1u, source.height / 2u);
    result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4u);

    const auto* src = reinterpret_cast<const uint8_t*>(source.pixels.data());
    auto* dst = reinterpret_cast<uint8_t*>(result.pixels.data());
    for (uint32_t y = 0; y < result.height; ++y) {
        const double y0 = static_cast<double>(y) * source.height / result.height;
        const double y1 = static_cast<double>(y + 1u) * source.height / result.height;
        const uint32_t sy0 = static_cast<uint32_t>(std::floor(y0));
        const uint32_t sy1 = std::min(source.height, static_cast<uint32_t>(std::ceil(y1)));
        for (uint32_t x = 0; x < result.width; ++x) {
            const double x0 = static_cast<double>(x) * source.width / result.width;
            const double x1 = static_cast<double>(x + 1u) * source.width / result.width;
            const uint32_t sx0 = static_cast<uint32_t>(std::floor(x0));
            const uint32_t sx1 = std::min(source.width, static_cast<uint32_t>(std::ceil(x1)));
            double sum[4]{};
            double totalWeight = 0.0;
            for (uint32_t sy = sy0; sy < sy1; ++sy) {
                const double wy = std::max(
                        0.0, std::min(y1, static_cast<double>(sy + 1u)) - std::max(y0, static_cast<double>(sy)));
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const double wx = std::max(
                            0.0, std::min(x1, static_cast<double>(sx + 1u)) - std::max(x0, static_cast<double>(sx)));
                    const double weight = wx * wy;
                    const size_t offset = (static_cast<size_t>(sy) * source.width + sx) * 4u;
                    for (size_t channel = 0; channel < 4; ++channel) {
                        float value = static_cast<float>(src[offset + channel]) / 255.0f;
                        if (srgb && channel < 3)
                            value = srgbToLinear(value);
                        sum[channel] += static_cast<double>(value) * weight;
                    }
                    totalWeight += weight;
                }
            }
            const size_t output = (static_cast<size_t>(y) * result.width + x) * 4u;
            for (size_t channel = 0; channel < 4; ++channel) {
                float value = totalWeight > 0.0 ? static_cast<float>(sum[channel] / totalWeight) : 0.0f;
                if (srgb && channel < 3)
                    value = linearToSrgb(value);
                dst[output + channel] = encode(value);
            }
        }
    }
    return result;
}

}  // namespace

std::vector<Rgba8MipLevel> buildRgba8MipChain(std::span<const std::byte> levelZero, uint32_t width, uint32_t height,
                                              bool srgb) {
    if (width == 0 || height == 0 || width > std::numeric_limits<size_t>::max() / height / 4u ||
        levelZero.size() != static_cast<size_t>(width) * height * 4u) {
        return {};
    }
    std::vector<Rgba8MipLevel> result;
    result.push_back({ width, height, std::vector<std::byte>(levelZero.begin(), levelZero.end()) });
    while (result.back().width > 1 || result.back().height > 1)
        result.push_back(downsample(result.back(), srgb));
    return result;
}

}  // namespace mulan::engine
