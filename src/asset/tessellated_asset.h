/**
 * @file brep_asset.h
 * @brief BRepAsset 保存导入形体的当前可渲染网格缓存。
 * @author hxxcxx
 * @date 2026-07-02
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/engine/geometry/mesh.h>

#include <utility>

namespace mulan::asset {

class BRepAsset : public GeometryAsset {
public:
    explicit BRepAsset(AssetId id, std::string name)
        : GeometryAsset(id, AssetKind::BRep, std::move(name)) {}

    const engine::Mesh& faceMesh() const { return face_mesh_; }
    const engine::Mesh& edgeMesh() const { return edge_mesh_; }

    void setRenderMeshes(engine::Mesh faceMesh, engine::Mesh edgeMesh) {
        face_mesh_ = std::move(faceMesh);
        edge_mesh_ = std::move(edgeMesh);
    }

private:
    engine::Mesh face_mesh_;
    engine::Mesh edge_mesh_;
};

} // namespace mulan::asset
