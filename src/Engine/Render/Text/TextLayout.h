/**
 * @file TextLayout.h
 * @brief 文字排版引擎 — UTF-8 字符串 → Quad 顶点列表
 * @author hxxcxx
 * @date 2026-04-27
 */

#pragma once

#include "TextTypes.h"
#include "FontAtlas.h"

#include <string_view>
#include <vector>

namespace MulanGeo::engine {

/// 文字排版引擎：将字符串 + 排版参数 → Quad 顶点列表
class TextLayout {
public:
    /// 排版一段文字，生成 Quad 顶点和索引
    /// @param font        已加载的字体图集
    /// @param text        UTF-8 文本
    /// @param x, y        起始位置（屏幕像素，左上角为原点）
    /// @param fontSize    目标字号（像素）
    /// @param color       RGBA 颜色（线性空间，0~1）
    /// @param outVertices 输出顶点列表（追加）
    /// @param outIndices  输出索引列表（追加）
    static void layout(
        const FontAtlas& font,
        std::string_view text,
        float x, float y,
        float fontSize,
        const float color[4],
        std::vector<TextVertex>& outVertices,
        std::vector<uint32_t>& outIndices);

    /// 测量文字宽度（用于对齐计算）
    static float measureWidth(
        const FontAtlas& font,
        std::string_view text,
        float fontSize);

private:
    /// UTF-8 解码，返回单个 Unicode 码点，ptr 自动前进
    static uint32_t decodeUtf8(const char*& ptr, const char* end);

    /// 将 float[4] RGBA 打包为 uint32_t RGBA8
    static uint32_t packColor(const float color[4]);
};

} // namespace MulanGeo::Engine
