/**
 * @file brep_asset.h
 * @brief BRepAsset —— 持有真正 B-Rep 拓扑(modeling::Shape)的几何资产。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * 与 TessellatedAsset 并存：后者只存离散网格缓存，不可参数化编辑；
 * 本类持有 modeling::Shape（B-Rep），Shape 变更后可重新离散刷新显示。
 *
 * 显示网格在首次 collectDrawables 时 lazy 离散，Shape 变更后标记 dirty 重新离散。
 * 离散通过 Shape::tessellate() 虚函数分发到具体内核后端（OCCT/truck），
 * 因此本资产只依赖 modeling_core（中立层），不依赖任何内核实现库。
 *
 * 参考：tessellated_asset.h（注入式离散缓存）、face_asset.h（eager 重建模式）。
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/modeling/core/shape.h>
#include <mulan/modeling/core/tessellation.h>

#include <utility>

namespace mulan::asset {

class BRepAsset : public GeometryAsset {
public:
    BRepAsset(AssetId id, std::string name, modeling::Shape shape);

    const modeling::Shape& shape() const { return shape_; }
    void setShape(modeling::Shape shape);

    const graphics::Mesh& solidMesh() const { return geometry_.solidMesh; }
    const graphics::Mesh& wireMesh() const { return geometry_.wireMesh; }

    bool empty() const { return shape_.empty(); }
    bool renderable() const { return !shape_.empty(); }

    void collectDrawables(std::vector<Drawable>& out) const override;
    math::AABB3 localBounds() const override;

private:
    /// lazy 离散：dirty 时调 Shape::tessellate 刷新 geometry_。返回是否成功离散。
    bool ensureTessellated() const;

    modeling::Shape shape_;
    mutable modeling::TessellationOptions tess_options_;
    mutable modeling::TessellatedGeometry geometry_;
    mutable bool geometry_dirty_ = true;
};

}  // namespace mulan::asset
