#include "text_geometry_data.h"
#include <mulan/engine/render/text/text_layout.h>

#include <cstring>

namespace mulan::document {

TextGeometryData::TextGeometryData(std::string text,
                                    float fontSize,
                                    const float color[4])
    : text_(std::move(text))
    , font_size_(fontSize)
{
    if (color) {
        std::memcpy(color_, color, sizeof(color_));
        has_color_ = true;
    } else {
        color_[0] = 1; color_[1] = 1; color_[2] = 1; color_[3] = 1;
    }
}

engine::Mesh TextGeometryData::faceMesh() const {
    if (!mesh_built_) {
        buildMesh();
    }
    return cached_mesh_;
}

void TextGeometryData::buildMesh() const {
    cached_mesh_ = engine::TextLayout::buildTextMesh(
        text_, font_size_,
        has_color_ ? color_ : nullptr);
    mesh_built_ = true;
}

} // namespace mulan::document
