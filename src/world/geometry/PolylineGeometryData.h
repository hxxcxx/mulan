/**
 * @file PolylineGeometryData.h
 * @brief 多段线几何数据
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../GeometryData.h"

#include <vector>

namespace mulan::world {

class PolylineGeometryData : public GeometryData {
public:
    PolylineGeometryData() = default;

    Type type() const override { return Type::Polyline; }

    engine::Mesh edgeMesh() const override;
    engine::Mesh faceMesh() const override { return {}; }
    engine::AABB bounds() const override;

    const std::vector<engine::Vec3>& points() const { return m_points; }
    void setPoints(std::vector<engine::Vec3> pts) { m_points = std::move(pts); }
    void addPoint(const engine::Vec3& pt) { m_points.push_back(pt); }

    bool closed() const { return m_closed; }
    void setClosed(bool c) { m_closed = c; }

private:
    std::vector<engine::Vec3> m_points;
    bool m_closed = false;
};

} // namespace mulan::world
