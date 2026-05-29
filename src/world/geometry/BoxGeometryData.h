/**
 * @file BoxGeometryData.h
 * @brief 参数化长方体（Box）几何数据
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../GeometryData.h"

namespace mulan::world {

class BoxGeometryData : public GeometryData {
public:
    BoxGeometryData(double w = 1.0, double h = 1.0, double d = 1.0);

    Type type() const override { return Type::Box; }

    engine::Mesh faceMesh() const override;
    engine::Mesh edgeMesh() const override;
    engine::AABB bounds() const override;

    bool setProperty(const std::string& name, double value) override;

    double width()  const { return m_width; }
    double height() const { return m_height; }
    double depth()  const { return m_depth; }

private:
    engine::Mesh generateFaceMesh() const;
    engine::Mesh generateEdgeMesh() const;

    double m_width  = 1.0;
    double m_height = 1.0;
    double m_depth  = 1.0;

    mutable uint64_t m_dataVersion   = 0;
    mutable uint64_t m_faceCacheVer  = 0;
    mutable uint64_t m_edgeCacheVer  = 0;
    mutable engine::Mesh m_cachedFaceMesh;
    mutable engine::Mesh m_cachedEdgeMesh;
};

} // namespace mulan::world
