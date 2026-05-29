/**
 * @file PolylineGeometryData.cpp
 * @brief 多段线几何实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "PolylineGeometryData.h"

namespace mulan::world {

engine::Mesh PolylineGeometryData::edgeMesh() const {
    engine::Mesh m;
    if (m_points.size() < 2) return m;

    m.vertexStride = sizeof(float) * 8;
    m.topology     = engine::PrimitiveTopology::LineList;

    for (auto& pt : m_points) {
        m.vertices.insert(m.vertices.end(), {
            static_cast<float>(pt.x), static_cast<float>(pt.y), static_cast<float>(pt.z),
            0.f, 0.f, 0.f, 0.f, 0.f});
    }

    int n = static_cast<int>(m_points.size());
    for (int i = 0; i < n - 1; ++i) {
        m.indices.push_back(i);
        m.indices.push_back(i + 1);
    }
    if (m_closed && n > 2) {
        m.indices.push_back(n - 1);
        m.indices.push_back(0);
    }

    m.computeBounds();
    return m;
}

engine::AABB PolylineGeometryData::bounds() const {
    engine::AABB b;
    for (auto& pt : m_points) b.expand(pt);
    return b;
}

} // namespace mulan::world
