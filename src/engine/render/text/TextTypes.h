/**
 * @file TextTypes.h
 * @brief MSDF 文字渲染公共类型 — 字形信息、顶点、绘制请求
 * @author hxxcxx
 * @date 2026-04-27
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mulan::engine {

// ============================================================
// 字形信息（由 FontAtlas 记录）
// ============================================================

struct GlyphInfo {
    uint32_t unicode     = 0;       ///< Unicode 码点
    float    advanceX    = 0;       ///< 水平步进（像素，基于 baseFontSize）
    float    advanceY    = 0;       ///< 垂直步进（像素，竖排用）

    // Atlas 纹理中的归一化 UV
    float    atlasU      = 0;       ///< 左上角 U
    float    atlasV      = 0;       ///< 左上角 V
    float    atlasU2     = 0;       ///< 右下角 U
    float    atlasV2     = 0;       ///< 右下角 V

    // 字形相对基线的偏移（像素，基于 baseFontSize）
    float    planeLeft   = 0;       ///< 水平偏移
    float    planeTop    = 0;       ///< 垂直偏移（向上为正）

    // 字形像素尺寸（基于 baseFontSize）
    float    width       = 0;
    float    height      = 0;
};

// ============================================================
// 文字顶点 — pos(2f) + uv(2f) + color(packed RGBA8) = 20 bytes
// ============================================================

struct TextVertex {
    float    x, y;                  ///< 屏幕空间位置（像素）
    float    u, v;                  ///< Atlas UV（归一化）
    uint32_t color;                 ///< 打包 RGBA8
};

// ============================================================
// 文字绘制请求 — 由上层调用 addText() 时内部暂存
// ============================================================

struct TextDrawItem {
    std::string text;               ///< UTF-8 文本
    float       x        = 0;       ///< 起始 X（像素，左起）
    float       y        = 0;       ///< 起始 Y（像素，上起）
    float       fontSize = 32.0f;   ///< 字号（像素）
    float       color[4] = {1,1,1,1}; ///< RGBA（线性空间）
};

} // namespace mulan::engine
