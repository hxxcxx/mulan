/**
 * @file text_types.h
 * @brief MSDF 文字渲染公共类型 — 字形信息、顶点、绘制请求
 * @author hxxcxx
 * @date 2026-04-27
 */

#pragma once

#include <mulan/math/math.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::engine {

// ============================================================
// 字形信息（由 FontAtlas 记录）
// ============================================================

struct GlyphInfo {
    uint32_t unicode = 0;  ///< Unicode 码点
    float advanceX = 0;    ///< 水平步进（像素，基于 baseFontSize）
    float advanceY = 0;    ///< 垂直步进（像素，竖排用）

    // Atlas 纹理中的归一化 UV
    float atlasU = 0;   ///< 左上角 U
    float atlasV = 0;   ///< 左上角 V
    float atlasU2 = 0;  ///< 右下角 U
    float atlasV2 = 0;  ///< 右下角 V

    // 字形相对基线的偏移（像素，基于 baseFontSize）
    float planeLeft = 0;  ///< 水平偏移
    float planeTop = 0;   ///< 垂直偏移（向上为正）

    // 字形像素尺寸（基于 baseFontSize）
    float width = 0;
    float height = 0;
};

// ============================================================
// 文字顶点 — pos(2f) + uv(2f) + color(packed RGBA8) = 20 bytes
// ============================================================

struct TextVertex {
    float x, y;      ///< 屏幕空间位置（像素）
    float u, v;      ///< Atlas UV（归一化）
    uint32_t color;  ///< 打包 RGBA8
};

// ============================================================
// 文字绘制请求 — 由上层调用 addText() 时内部暂存
// ============================================================

struct TextDrawItem {
    std::string text;                 ///< UTF-8 文本
    float x = 0;                      ///< 起始 X（像素，左起）
    float y = 0;                      ///< 起始 Y（像素，上起）
    float fontSize = 32.0f;           ///< 字号（像素）
    float color[4] = { 1, 1, 1, 1 };  ///< RGBA（线性空间）
};

enum class TextAnchor : uint8_t {
    TopLeft,
    Center,
    CenterLeft,
    CenterRight,
};

enum class TextSpace : uint8_t {
    Screen,
    WorldBillboard,
    WorldPlanar,
};

enum class TextDepthMode : uint8_t {
    AlwaysOnTop,
    // Reserved for a future depth-tested text pipeline; current TextStage downgrades it to AlwaysOnTop.
    TestDepth,
};

struct TextDrawDesc {
    static TextDrawDesc screen(std::string_view text, const math::Point2& positionPx, float sizePx,
                               const math::Vec4& color = math::Vec4(1.0, 1.0, 1.0, 1.0),
                               TextAnchor anchor = TextAnchor::Center, std::string_view font = "default") {
        TextDrawDesc desc;
        desc.text = std::string(text);
        desc.font = std::string(font);
        desc.space = TextSpace::Screen;
        desc.anchor = anchor;
        desc.depthMode = TextDepthMode::AlwaysOnTop;
        desc.positionPx = positionPx;
        desc.sizePx = sizePx;
        desc.color = color;
        return desc;
    }

    static TextDrawDesc worldPlanar(std::string_view text, const math::Point3& positionWorld,
                                    const math::Vec3& rightWorld, const math::Vec3& upWorld,
                                    const math::Mat4& clipFromWorld, const math::Point2& viewportOriginPx,
                                    const math::Vec2& viewportSizePx, float sizePx, float sizeWorld,
                                    const math::Vec4& color = math::Vec4(1.0, 1.0, 1.0, 1.0),
                                    TextAnchor anchor = TextAnchor::Center, std::string_view font = "default") {
        TextDrawDesc desc;
        desc.text = std::string(text);
        desc.font = std::string(font);
        desc.space = TextSpace::WorldPlanar;
        desc.anchor = anchor;
        desc.depthMode = TextDepthMode::AlwaysOnTop;
        desc.positionWorld = positionWorld;
        desc.rightWorld = rightWorld;
        desc.upWorld = upWorld;
        desc.clipFromWorld = clipFromWorld;
        desc.viewportOriginPx = viewportOriginPx;
        desc.viewportSizePx = viewportSizePx;
        desc.sizePx = sizePx;
        desc.sizeWorld = sizeWorld;
        desc.color = color;
        return desc;
    }

    std::string text;
    std::string font = "default";
    TextSpace space = TextSpace::Screen;
    TextAnchor anchor = TextAnchor::Center;
    TextDepthMode depthMode = TextDepthMode::AlwaysOnTop;
    math::Point2 positionPx{};
    math::Point3 positionWorld{};
    math::Vec3 rightWorld{ 1.0, 0.0, 0.0 };
    math::Vec3 upWorld{ 0.0, 1.0, 0.0 };
    math::Mat4 clipFromWorld = math::Mat4::identity();
    math::Point2 viewportOriginPx{};
    math::Vec2 viewportSizePx{};
    float sizePx = 14.0f;
    float sizeWorld = 1.0f;
    math::Vec4 color{ 1.0, 1.0, 1.0, 1.0 };
};

class TextDrawList {
public:
    void clear() { items_.clear(); }
    void add(const TextDrawDesc& desc) {
        if (!desc.text.empty()) {
            items_.push_back(desc);
        }
    }
    bool empty() const { return items_.empty(); }
    const std::vector<TextDrawDesc>& items() const { return items_; }

private:
    std::vector<TextDrawDesc> items_;
};

}  // namespace mulan::engine
