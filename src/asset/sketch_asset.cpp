#include "sketch_asset.h"

#include <mulan/graphics/vertex/vertex_buffer.h>

namespace mulan::asset {
namespace {

void writePoint(graphics::VertexBufferBuilder& vb, uint32_t index, const math::Point3& point) {
    vb.setPosition(index, static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z));
    vb.setNormal(index, 0.0f, 0.0f, 1.0f);
    float uv[2] = { 0.0f, 0.0f };
    vb.write(index, graphics::VertexSemantic::TexCoord0, uv);
}

}  // namespace

SketchElementId SketchAsset::addLine(const math::Point3& start, const math::Point3& end) {
    SketchLine line;
    line.id = allocateElementId();
    line.start = start;
    line.end = end;
    lines_.push_back(line);
    rebuildRenderMesh();
    return line.id;
}

bool SketchAsset::updateLine(SketchElementId id, const math::Point3& start, const math::Point3& end) {
    for (auto& line : lines_) {
        if (line.id != id) {
            continue;
        }
        line.start = start;
        line.end = end;
        rebuildRenderMesh();
        return true;
    }
    return false;
}

void SketchAsset::collectDrawables(std::vector<Drawable>& out) const {
    if (!wire_mesh_.empty()) {
        out.push_back({ &wire_mesh_, AssetId::invalid(), DrawableRole::Wire });
    }
}

math::AABB3 SketchAsset::localBounds() const {
    return wire_mesh_.bounds;
}

void SketchAsset::rebuildRenderMesh() {
    wire_mesh_ = {};
    if (lines_.empty()) {
        return;
    }

    const uint32_t vertexCount = static_cast<uint32_t>(lines_.size() * 2);
    const uint32_t indexCount = vertexCount;

    wire_mesh_.layout = graphics::layouts::surface();
    wire_mesh_.topology = graphics::PrimitiveTopology::LineList;
    wire_mesh_.indexType = graphics::IndexType::UInt32;

    graphics::VertexBufferBuilder vb(wire_mesh_.layout, vertexCount);
    wire_mesh_.indices.resize(static_cast<size_t>(indexCount) * sizeof(uint32_t));
    auto* indices = reinterpret_cast<uint32_t*>(wire_mesh_.indices.data());

    uint32_t vertex = 0;
    for (const auto& line : lines_) {
        writePoint(vb, vertex, line.start);
        writePoint(vb, vertex + 1, line.end);
        indices[vertex] = vertex;
        indices[vertex + 1] = vertex + 1;
        vertex += 2;
    }

    auto bytes = vb.data();
    wire_mesh_.vertices.assign(bytes.begin(), bytes.end());
    wire_mesh_.computeBounds();
}

SketchElementId SketchAsset::allocateElementId() {
    SketchElementId id = next_element_id_;
    ++next_element_id_.value;
    return id;
}

}  // namespace mulan::asset
