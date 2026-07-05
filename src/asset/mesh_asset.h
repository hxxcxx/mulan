/**
 * @file mesh_asset.h
 * @brief MeshAsset 保存导入网格资产及其 primitive 列表。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/graphics/mesh.h>

#include <string>
#include <utility>
#include <vector>

namespace mulan::asset {

struct MeshPrimitive {
    graphics::Mesh mesh;
    AssetId material = AssetId::invalid();
    std::string name;
};

class MeshAsset : public GeometryAsset {
public:
    MeshAsset(AssetId id, std::string name, graphics::Mesh mesh = {})
        : GeometryAsset(id, AssetKind::Mesh, std::move(name)) {
        if (!mesh.empty()) {
            addPrimitive(std::move(mesh));
        }
    }

    MeshAsset(AssetId id, std::string name, std::vector<MeshPrimitive> primitives)
        : GeometryAsset(id, AssetKind::Mesh, std::move(name)), primitives_(std::move(primitives)) {}

    const std::vector<MeshPrimitive>& primitives() const { return primitives_; }
    std::vector<MeshPrimitive>& primitives() { return primitives_; }

    bool empty() const { return primitives_.empty(); }
    size_t primitiveCount() const { return primitives_.size(); }

    /// 每个 primitive 产出一段实体填充网格，使用 primitive 各自的材质。
    void collectDrawables(std::vector<Drawable>& out) const override {
        for (const auto& p : primitives_) {
            if (!p.mesh.empty())
                out.push_back({ &p.mesh, p.material, DrawableRole::Solid });
        }
    }

    /// 本地包围盒 = 所有 primitive 网格 bounds 的并集（mesh.bounds 在导入时已算好）。
    math::AABB3 localBounds() const override {
        math::AABB3 b = math::AABB3::empty();
        for (const auto& p : primitives_) {
            if (!p.mesh.bounds.isEmpty())
                b.expand(p.mesh.bounds);
        }
        return b;
    }

    MeshPrimitive& addPrimitive(graphics::Mesh mesh, AssetId material = AssetId::invalid(), std::string name = {}) {
        primitives_.push_back(MeshPrimitive{ std::move(mesh), material, std::move(name) });
        return primitives_.back();
    }

    const graphics::Mesh& mesh() const { return primitives_.front().mesh; }
    graphics::Mesh& mesh() { return ensureDefaultPrimitive().mesh; }

private:
    MeshPrimitive& ensureDefaultPrimitive() {
        if (primitives_.empty()) {
            primitives_.push_back({});
        }
        return primitives_.front();
    }

    std::vector<MeshPrimitive> primitives_;
};

}  // namespace mulan::asset
