/**
 * @file TextGeometryData.cpp
 * @brief 文字几何数据实现
 * @author hxxcxx
 * @date 2026-06-30
 */

#include "TextGeometryData.h"
#include <mulan/engine/render/text/TextLayout.h>

#include <cstring>

namespace mulan::document {

TextGeometryData::TextGeometryData(std::string text,
                                    float fontSize,
                                    const float color[4])
    : m_text(std::move(text))
    , m_fontSize(fontSize)
{
    if (color) {
        std::memcpy(m_color, color, sizeof(m_color));
        m_hasColor = true;
    } else {
        m_color[0] = 1; m_color[1] = 1; m_color[2] = 1; m_color[3] = 1;
    }
}

engine::Mesh TextGeometryData::faceMesh() const {
    if (!m_meshBuilt) {
        buildMesh();
    }
    return m_cachedMesh;
}

void TextGeometryData::buildMesh() const {
    m_cachedMesh = engine::TextLayout::buildTextMesh(
        m_text, m_fontSize,
        m_hasColor ? m_color : nullptr);
    m_meshBuilt = true;
}

} // namespace mulan::document
