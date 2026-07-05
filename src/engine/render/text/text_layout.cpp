#include "text_layout.h"
#include "font_manager.h"
#include "../../vertex/vertex_buffer.h"
#include "../../vertex/vertex_semantic.h"

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mulan::engine {

// ============================================================
// UTF-8 解码
// ============================================================

uint32_t TextLayout::decodeUtf8(const char*& ptr, const char* end) {
    if (ptr >= end) return 0;

    uint8_t c = static_cast<uint8_t>(*ptr++);

    // 1-byte: 0xxxxxxx
    if (c < 0x80) return c;

    // 2-byte: 110xxxxx 10xxxxxx
    if ((c & 0xE0) == 0xC0) {
        if (ptr >= end) return 0;
        uint8_t c2 = static_cast<uint8_t>(*ptr++);
        if ((c2 & 0xC0) != 0x80) return 0;
        return ((uint32_t)(c & 0x1F) << 6) | (c2 & 0x3F);
    }

    // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
    if ((c & 0xF0) == 0xE0) {
        if (ptr + 1 >= end) { ptr = end; return 0; }
        uint8_t c2 = static_cast<uint8_t>(*ptr++);
        uint8_t c3 = static_cast<uint8_t>(*ptr++);
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
    }

    // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((c & 0xF8) == 0xF0) {
        if (ptr + 2 >= end) { ptr = end; return 0; }
        uint8_t c2 = static_cast<uint8_t>(*ptr++);
        uint8_t c3 = static_cast<uint8_t>(*ptr++);
        uint8_t c4 = static_cast<uint8_t>(*ptr++);
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) return 0;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c2 & 0x3F) << 12)
             | ((uint32_t)(c3 & 0x3F) << 6) | (c4 & 0x3F);
    }

    return 0; // 非法 UTF-8
}

// ============================================================
// 颜色打包
// ============================================================

uint32_t TextLayout::packColor(const float color[4]) {
    auto clamp255 = [](float v) -> uint32_t {
        return static_cast<uint32_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
    };
    return clamp255(color[0]) << 0
         | clamp255(color[1]) << 8
         | clamp255(color[2]) << 16
         | clamp255(color[3]) << 24;
}

// ============================================================
// 排版生成 Quad
// ============================================================

void TextLayout::layout(
    const FontAtlas& font,
    std::string_view text,
    float x, float y,
    float fontSize,
    const float color[4],
    std::vector<TextVertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    float scale = fontSize / font.baseFontSize();
    uint32_t packedColor = packColor(color);

    const char* ptr = text.data();
    const char* end = ptr + text.size();
    float cursorX = x;
    float cursorY = y;
    uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

    while (ptr < end) {
        uint32_t cp = decodeUtf8(ptr, end);
        if (cp == 0) break;

        // 换行
        if (cp == '\n') {
            cursorX = x;
            cursorY += fontSize * 1.2f;
            continue;
        }

        // 回车（忽略）
        if (cp == '\r') continue;

        const GlyphInfo* glyph = font.getGlyph(cp);
        if (!glyph) {
            // 未找到字形，空一格
            cursorX += fontSize * 0.5f * scale;
            continue;
        }

        // 跳过宽度为 0 的字形
        if (glyph->width <= 0 || glyph->height <= 0) {
            cursorX += glyph->advanceX * scale;
            continue;
        }

        // 计算 Quad 四个角（屏幕坐标，Y 向下）
        // planeTop 是 Y-up 中的偏移（基线到字形顶部），在 Y-down 中翻转
        float qx = cursorX + glyph->planeLeft * scale;
        float qy = cursorY - glyph->planeTop * scale;  // planeTop > 0 → y 向上 → 屏幕 y 减小
        float qw = glyph->width * scale;
        float qh = glyph->height * scale;

        // 4 个顶点：左上、右上、右下、左下
        outVertices.push_back({qx,      qy,      glyph->atlasU,  glyph->atlasV,  packedColor});
        outVertices.push_back({qx + qw, qy,      glyph->atlasU2, glyph->atlasV,  packedColor});
        outVertices.push_back({qx + qw, qy + qh, glyph->atlasU2, glyph->atlasV2, packedColor});
        outVertices.push_back({qx,      qy + qh, glyph->atlasU,  glyph->atlasV2, packedColor});

        // 2 个三角形（6 个索引）
        outIndices.push_back(baseIdx + 0);
        outIndices.push_back(baseIdx + 1);
        outIndices.push_back(baseIdx + 2);
        outIndices.push_back(baseIdx + 0);
        outIndices.push_back(baseIdx + 2);
        outIndices.push_back(baseIdx + 3);

        baseIdx += 4;
        cursorX += glyph->advanceX * scale;
    }
}

// ============================================================
// 测量文字宽度
// ============================================================

float TextLayout::measureWidth(
    const FontAtlas& font,
    std::string_view text,
    float fontSize)
{
    float scale = fontSize / font.baseFontSize();
    float width = 0;

    const char* ptr = text.data();
    const char* end = ptr + text.size();

    while (ptr < end) {
        uint32_t cp = decodeUtf8(ptr, end);
        if (cp == 0) break;
        if (cp == '\n' || cp == '\r') continue;

        const GlyphInfo* glyph = font.getGlyph(cp);
        if (glyph) {
            width += glyph->advanceX * scale;
        } else {
            width += fontSize * 0.5f * scale;
        }
    }

    return width;
}

// ============================================================
// buildTextMesh — 公开 API
// ============================================================

graphics::Mesh TextLayout::buildTextMesh(std::string_view text,
                                float fontSize,
                                const float color[4]) {
    graphics::Mesh mesh;
    mesh.layout    = layouts::surface();
    mesh.topology  = PrimitiveTopology::TriangleList;
    mesh.indexType = IndexType::UInt32;

    auto* font = FontManager::instance().defaultFont();
    if (!font || !font->isLoaded()) return mesh;

    // 排版得到 TextVertex 列表
    float defaultColor[4] = {1, 1, 1, 1};
    const float* c = color ? color : defaultColor;

    std::vector<TextVertex> textVerts;
    std::vector<uint32_t> textIndices;
    layout(*font, text, 0, 0, fontSize, c, textVerts, textIndices);

    if (textVerts.empty()) return mesh;

    // 按 surface 布局写入：pos(3f) + normal(3f) + uv(2f)
    VertexBufferBuilder vb(mesh.layout, static_cast<uint32_t>(textVerts.size()));
    for (uint32_t i = 0; i < textVerts.size(); ++i) {
        const auto& tv = textVerts[i];
        vb.setPosition(i, tv.x, tv.y, 0.0f);           // pos.z = 0 (flat)
        vb.setNormal  (i, 0.0f, 0.0f, 1.0f);           // normal 朝 +Z
        float uv[2] = {tv.u, tv.v};
        vb.write(i, VertexSemantic::TexCoord0, uv);
    }
    auto vertBytes = vb.data();
    mesh.vertices.assign(vertBytes.begin(), vertBytes.end());

    // 索引直接搬字节
    const std::byte* ib = reinterpret_cast<const std::byte*>(textIndices.data());
    mesh.indices.assign(ib, ib + textIndices.size() * sizeof(uint32_t));

    mesh.computeBounds();
    return mesh;
}

} // namespace mulan::engine
