/**
 * @file tessellated_asset.h
 * @brief TessellatedAsset 保存 CAD 形体经网格化（tessellation）后的可渲染网格缓存。
 *
 * 注意：本类**不持有** B-Rep 拓扑（face/edge/vertex 的参数化表示），仅保存
 * 导入时一次性生成的实体填充网格（三角）+ 线框网格（线段）。因此不可参数化编辑；
 * 若将来需要参数化编辑，应另建一个持有真正 B-Rep 拓扑的资产类，二者并存。
 *
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/graphics/mesh.h>

#include <utility>

namespace mulan::asset {

/// 来自 CAD 形体（STEP/IGES）经网格化后的显示资产：固定含面网格 + 边网格。
class TessellatedAsset : public GeometryAsset {
public:
    explicit TessellatedAsset(AssetId id, std::string name)
        : GeometryAsset(id, AssetKind::Tessellated, std::move(name)) {}

    const graphics::Mesh& solidMesh() const { return solid_mesh_; }
    const graphics::Mesh& wireMesh() const { return wire_mesh_; }

    void setRenderMeshes(graphics::Mesh solidMesh, graphics::Mesh wireMesh) {
        if (sameMesh(solid_mesh_, solidMesh) && sameMesh(wire_mesh_, wireMesh))
            return;
        touch();
        solid_mesh_ = std::move(solidMesh);
        wire_mesh_ = std::move(wireMesh);
    }

    /// 产出两段：实体填充网格（Solid）+ 线框网格（Wire），均无专属材质。
    void collectDrawables(std::vector<Drawable>& out) const override {
        if (!solid_mesh_.empty())
            out.push_back({ &solid_mesh_, AssetId::invalid(), DrawableRole::Solid });
        if (!wire_mesh_.empty())
            out.push_back({ &wire_mesh_, AssetId::invalid(), DrawableRole::Wire });
    }

    /// 本地包围盒 = solid + wire 网格顶点的并集（mesh.bounds 在导入时已算好）。
    math::AABB3 localBounds() const override {
        math::AABB3 b = math::AABB3::empty();
        if (!solid_mesh_.bounds.isEmpty())
            b.expand(solid_mesh_.bounds);
        if (!wire_mesh_.bounds.isEmpty())
            b.expand(wire_mesh_.bounds);
        return b;
    }

private:
    static bool sameMesh(const graphics::Mesh& a, const graphics::Mesh& b) {
        if (a.layout.stride() != b.layout.stride() || a.layout.attrCount() != b.layout.attrCount() ||
            a.layout.bufferCount() != b.layout.bufferCount()) {
            return false;
        }
        for (uint8_t i = 0; i < a.layout.attrCount(); ++i) {
            if (!(a.layout[i] == b.layout[i]))
                return false;
        }
        return a.vertices == b.vertices && a.indices == b.indices && a.indexType == b.indexType &&
               a.topology == b.topology && a.bounds.min == b.bounds.min && a.bounds.max == b.bounds.max;
    }

    graphics::Mesh solid_mesh_;
    graphics::Mesh wire_mesh_;
};

}  // namespace mulan::asset
