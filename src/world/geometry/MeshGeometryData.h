/**
 * @file MeshGeometryData.h
 * @brief 预三角化网格（OBJ/STL/glTF）几何数据
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../GeometryData.h"

namespace mulan::world {

class MeshGeometryData : public GeometryData {
public:
    MeshGeometryData() = default;
    explicit MeshGeometryData(engine::Mesh mesh);

    Type type() const override { return Type::Mesh; }

    engine::Mesh faceMesh() const override;
    engine::Mesh edgeMesh() const override { return {}; }
    engine::AABB bounds() const override;

    void setMesh(engine::Mesh mesh) { m_mesh = std::move(mesh); m_mesh.computeBounds(); }
    const engine::Mesh& mesh() const { return m_mesh; }

private:
    engine::Mesh m_mesh;
};

} // namespace mulan::world
