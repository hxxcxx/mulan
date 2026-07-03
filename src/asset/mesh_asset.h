/**
 * @file mesh_asset.h
 * @brief MeshAsset 保存导入网格资产及其 primitive 列表。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/engine/geometry/mesh.h>

#include <string>
#include <utility>
#include <vector>

namespace mulan::asset {

struct MeshPrimitive {
    engine::Mesh mesh;
    AssetId material = AssetId::invalid();
    std::string name;
};

class MeshAsset : public GeometryAsset {
public:
    MeshAsset(AssetId id, std::string name, engine::Mesh mesh = {})
        : GeometryAsset(id, AssetKind::Mesh, std::move(name))
    {
        if (!mesh.empty()) {
            addPrimitive(std::move(mesh));
        }
    }

    const std::vector<MeshPrimitive>& primitives() const { return primitives_; }
    std::vector<MeshPrimitive>& primitives() { return primitives_; }

    bool empty() const { return primitives_.empty(); }
    size_t primitiveCount() const { return primitives_.size(); }

    MeshPrimitive& addPrimitive(engine::Mesh mesh,
                                AssetId material = AssetId::invalid(),
                                std::string name = {}) {
        primitives_.push_back(MeshPrimitive{std::move(mesh), material, std::move(name)});
        return primitives_.back();
    }

    const engine::Mesh& mesh() const { return primitives_.front().mesh; }
    engine::Mesh& mesh() { return ensureDefaultPrimitive().mesh; }

private:
    MeshPrimitive& ensureDefaultPrimitive() {
        if (primitives_.empty()) {
            primitives_.push_back({});
        }
        return primitives_.front();
    }

    std::vector<MeshPrimitive> primitives_;
};

} // namespace mulan::asset
