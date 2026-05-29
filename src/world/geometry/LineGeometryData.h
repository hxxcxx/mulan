/**
 * @file LineGeometryData.h
 * @brief 线段几何数据
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../GeometryData.h"

namespace mulan::world {

class LineGeometryData : public GeometryData {
public:
    LineGeometryData() = default;
    LineGeometryData(const engine::Vec3& s, const engine::Vec3& e);

    Type type() const override { return Type::Line; }

    engine::Mesh edgeMesh() const override;
    engine::Mesh faceMesh() const override { return {}; }
    engine::AABB bounds() const override;

    const engine::Vec3& start() const { return m_start; }
    const engine::Vec3& end()   const { return m_end; }
    void set(const engine::Vec3& s, const engine::Vec3& e);

private:
    engine::Vec3 m_start{0, 0, 0};
    engine::Vec3 m_end{1, 0, 0};
};

} // namespace mulan::world
