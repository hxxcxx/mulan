/**
 * @file MeshGeometryData.cpp
 * @brief 预三角化网格实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "MeshGeometryData.h"

namespace mulan::world {

MeshGeometryData::MeshGeometryData(engine::Mesh mesh)
    : m_mesh(std::move(mesh)) {
    m_mesh.computeBounds();
}

engine::Mesh MeshGeometryData::faceMesh() const {
    return m_mesh;
}

engine::AABB MeshGeometryData::bounds() const {
    return m_mesh.bounds;
}

} // namespace mulan::world
