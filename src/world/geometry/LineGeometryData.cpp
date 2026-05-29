/**
 * @file LineGeometryData.cpp
 * @brief 线段几何实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "LineGeometryData.h"

namespace mulan::world {

LineGeometryData::LineGeometryData(const engine::Vec3& s, const engine::Vec3& e)
    : m_start(s), m_end(e) {}

void LineGeometryData::set(const engine::Vec3& s, const engine::Vec3& e) {
    m_start = s; m_end = e;
}

engine::Mesh LineGeometryData::edgeMesh() const {
    engine::Mesh m;
    m.vertexStride = sizeof(float) * 8;
    m.topology     = engine::PrimitiveTopology::LineList;

    m.vertices = {
        static_cast<float>(m_start.x), static_cast<float>(m_start.y), static_cast<float>(m_start.z),
        0.f, 0.f, 0.f, 0.f, 0.f,
        static_cast<float>(m_end.x), static_cast<float>(m_end.y), static_cast<float>(m_end.z),
        0.f, 0.f, 0.f, 0.f, 0.f,
    };
    m.indices = {0, 1};
    m.computeBounds();
    return m;
}

engine::AABB LineGeometryData::bounds() const {
    engine::AABB b;
    b.expand(m_start);
    b.expand(m_end);
    return b;
}

} // namespace mulan::world
