/**
 * @file ArcGeometryData.cpp
 * @brief 圆弧几何实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "ArcGeometryData.h"

#include <cmath>

namespace mulan::world {

ArcGeometryData::ArcGeometryData(const engine::Vec3& center, double radius,
                                 double startAngle, double endAngle)
    : m_center(center), m_radius(radius),
      m_startAngle(startAngle), m_endAngle(endAngle) {}

void ArcGeometryData::set(const engine::Vec3& center, double radius,
                           double startAngle, double endAngle) {
    m_center     = center;
    m_radius     = radius;
    m_startAngle = startAngle;
    m_endAngle   = endAngle;
}

engine::Mesh ArcGeometryData::edgeMesh() const {
    engine::Mesh m;
    m.vertexStride = sizeof(float) * 8;
    m.topology     = engine::PrimitiveTopology::LineList;

    double span = m_endAngle - m_startAngle;
    int n = std::max(4, static_cast<int>(span / (2.0 * 3.14159) * m_segments));

    for (int i = 0; i <= n; ++i) {
        double t = m_startAngle + (span * i) / n;
        double x = m_center.x + m_radius * std::cos(t);
        double y = m_center.y + m_radius * std::sin(t);
        m.vertices.insert(m.vertices.end(), {
            static_cast<float>(x), static_cast<float>(y), static_cast<float>(m_center.z),
            0.f, 0.f, 0.f, 0.f, 0.f});
        // 除第一个外的每个点都跟前一点连线
        if (i > 0) {
            uint32_t base = static_cast<uint32_t>(i - 1);
            m.indices.push_back(base);
            m.indices.push_back(base + 1);
        }
    }

    m.computeBounds();
    return m;
}

engine::AABB ArcGeometryData::bounds() const {
    engine::AABB b;
    // 简单用端点 + 几个关键角度
    double r = std::abs(m_radius);
    for (int i = 0; i <= m_segments; ++i) {
        double t = m_startAngle + (m_endAngle - m_startAngle) * i / m_segments;
        engine::Vec3 p(m_center.x + r * std::cos(t),
                       m_center.y + r * std::sin(t), m_center.z);
        b.expand(p);
    }
    return b;
}

} // namespace mulan::world
