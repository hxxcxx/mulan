/**
 * @file text_geometry_data.h
 * @brief 文字几何数据 — 惰性 Mesh 生成
 * @author hxxcxx
 * @date 2026-06-30
 */

#pragma once

#include <mulan/world/geometry_data.h>
#include <mulan/engine/geometry/mesh.h>

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

    const std::string& text() const { return text_; }
    float fontSize() const { return font_size_; }

private:
    void buildMesh() const;

    std::string  text_;
    float        font_size_;
    float        color_[4];
    bool         has_color_ = false;

    mutable engine::Mesh cached_mesh_;
    mutable bool         mesh_built_ = false;
};

} // namespace mulan::document
