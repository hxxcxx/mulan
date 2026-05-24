/**
 * @file TorusGeometry.h
 * @brief 圆环体几何体 — 参数化（中心 + 主半径 + 次半径）
 * @author hxxcxx
 * @date 2026-05-19
 */
#pragma once

#include "DocumentExport.h"
#include "Geometry.h"

#include "MulanGeo/Engine/Geometry/PrimitiveMesh.h"
#include "MulanGeo/Core/Serialization/Archive.h"

#include <memory>

namespace MulanGeo::Document {

class DOCUMENT_API TorusGeometry : public Geometry {
    MULANGEO_OBJECT(TorusGeometry, Geometry)

public:
    TorusGeometry() = default;

    TorusGeometry(const engine::Vec3& center, double majorRadius, double minorRadius)
        : m_center(center), m_majorRadius(majorRadius), m_minorRadius(minorRadius) {}

    const engine::Vec3& center() const { return m_center; }
    double majorRadius() const { return m_majorRadius; }
    double minorRadius() const { return m_minorRadius; }

    void setParameters(const engine::Vec3& c, double R, double r) {
        m_center = c; m_majorRadius = R; m_minorRadius = r; invalidateCache();
    }

    GeometryType geometryType() const override { return GeometryType::Torus; }

    const engine::Mesh* displayMesh() const override {
        if (!m_cachedMesh) {
            m_cachedMesh = engine::PrimitiveMesh::torus(m_majorRadius, m_minorRadius);
        }
        return m_cachedMesh.get();
    }

    engine::AABB boundingBox() const override {
        double outer = m_majorRadius + m_minorRadius;
        return engine::AABB(
            m_center - engine::Vec3(outer, m_minorRadius, outer),
            m_center + engine::Vec3(outer, m_minorRadius, outer)
        );
    }

    void serialize(core::OutputArchive& ar) const override {
        ar << m_center.x << m_center.y << m_center.z;
        ar << m_majorRadius << m_minorRadius;
    }

    void serialize(core::InputArchive& ar) override {
        ar >> m_center.x >> m_center.y >> m_center.z;
        ar >> m_majorRadius >> m_minorRadius;
        invalidateCache();
    }

private:
    void invalidateCache() { m_cachedMesh.reset(); }

    engine::Vec3 m_center{0.0};
    double m_majorRadius = 1.0;
    double m_minorRadius = 0.3;
    mutable std::unique_ptr<engine::Mesh> m_cachedMesh;
};

} // namespace MulanGeo::Document
