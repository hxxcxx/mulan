/**
 * @file ArcGeometryData.h
 * @brief 圆弧几何数据 — 离散采样为线段
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../GeometryData.h"

namespace mulan::world {

class ArcGeometryData : public GeometryData {
public:
    ArcGeometryData() = default;
    ArcGeometryData(const engine::Vec3& center, double radius,
                    double startAngle, double endAngle);

    Type type() const override { return Type::Arc; }

    engine::Mesh edgeMesh() const override;
    engine::Mesh faceMesh() const override { return {}; }
    engine::AABB bounds() const override;

    void set(const engine::Vec3& center, double radius,
             double startAngle, double endAngle);

    int segments() const { return m_segments; }
    void setSegments(int n) { m_segments = n < 4 ? 4 : n; }

private:
    engine::Vec3 m_center{0, 0, 0};
    double m_radius     = 1.0;
    double m_startAngle = 0.0;
    double m_endAngle   = 3.14159;
    int    m_segments   = 32;
};

} // namespace mulan::world
