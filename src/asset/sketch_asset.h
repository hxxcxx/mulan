/**
 * @file sketch_asset.h
 * @brief SketchAsset 保存可编辑草图元素，并派生可渲染线框网格
 * @author hxxcxx
 * @date 2026-07-07
 *
 * SketchAsset 是编辑语义资产，不只是渲染缓存。第一阶段仅支持线段，后续会扩展为
 * polyline、rectangle、circle、face profile 等元素。渲染网格由草图元素重建而来，
 * 不能作为长期编辑真相。
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mulan::asset {

enum class SketchElementKind : uint8_t {
    Line,
};

struct SketchElementId {
    uint32_t value = 0;

    static constexpr SketchElementId invalid() { return {}; }
    constexpr bool valid() const { return value != 0; }

    friend constexpr bool operator==(SketchElementId a, SketchElementId b) { return a.value == b.value; }
    friend constexpr bool operator!=(SketchElementId a, SketchElementId b) { return !(a == b); }
};

struct SketchLine {
    SketchElementId id;
    math::Point3 start;
    math::Point3 end;
};

class SketchAsset : public GeometryAsset {
public:
    explicit SketchAsset(AssetId id, std::string name) : GeometryAsset(id, AssetKind::Sketch, std::move(name)) {}

    SketchElementId addLine(const math::Point3& start, const math::Point3& end);
    bool updateLine(SketchElementId id, const math::Point3& start, const math::Point3& end);

    const std::vector<SketchLine>& lines() const { return lines_; }

    void collectDrawables(std::vector<Drawable>& out) const override;
    math::AABB3 localBounds() const override;

private:
    void rebuildRenderMesh();

    SketchElementId allocateElementId();

    SketchElementId next_element_id_{ 1 };
    std::vector<SketchLine> lines_;
    graphics::Mesh wire_mesh_;
};

}  // namespace mulan::asset
