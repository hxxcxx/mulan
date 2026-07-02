/**
 * @file mesh_asset.h
 * @brief MeshAsset —— 导入多边形网格数据的几何资产骨架
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "geometry_asset.h"

#include <mulan/engine/geometry/mesh.h>

#include <utility>

namespace mulan::asset {

class MeshAsset : public GeometryAsset {
public:
    MeshAsset(AssetId id, std::string name, engine::Mesh mesh = {})
        : GeometryAsset(id, AssetKind::Mesh, std::move(name))
        , mesh_(std::move(mesh)) {}

    const engine::Mesh& mesh() const { return mesh_; }
    engine::Mesh& mesh() { return mesh_; }

private:
    engine::Mesh mesh_;
};

} // namespace mulan::asset
