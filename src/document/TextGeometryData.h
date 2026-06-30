/**
 * @file TextGeometryData.h
 * @brief 文字几何数据 — 惰性 Mesh 生成
 * @author hxxcxx
 * @date 2026-06-30
 */

#pragma once

#include <mulan/world/GeometryData.h>
#include <mulan/engine/geometry/Mesh.h>

#include <string>

namespace mulan::document {

class TextGeometryData : public world::GeometryData {
public:
    TextGeometryData(std::string text,
                     float fontSize = 24.0f,
                     const float color[4] = nullptr);

    ~TextGeometryData() override = default;

    Type type() const override { return Type::Text; }

    engine::Mesh faceMesh() const override;

    const std::string& text() const { return m_text; }
    float fontSize() const { return m_fontSize; }

private:
    void buildMesh() const;

    std::string  m_text;
    float        m_fontSize;
    float        m_color[4];
    bool         m_hasColor = false;

    mutable engine::Mesh m_cachedMesh;
    mutable bool         m_meshBuilt = false;
};

} // namespace mulan::document
