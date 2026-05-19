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

    TorusGeometry(const Engine::Vec3& center, double majorRadius, double minorRadius)
        : m_center(center), m_majorRadius(majorRadius), m_minorRadius(minorRadius) {}

    const Engine::Vec3& center() const { return m_center; }
    double majorRadius() const { return m_majorRadius; }
    double minorRadius() const { return m_minorRadius; }

    void setParameters(const Engine::Vec3& c, double R, double r) {
        m_center = c; m_majorRadius = R; m_minorRadius = r; invalidateCache();
    }

    GeometryType geometryType() const override { return GeometryType::Torus; }

    const Engine::Mesh* displayMesh() const override {
        if (!m_cachedMesh) {
            m_cachedMesh = Engine::PrimitiveMesh::torus(m_majorRadius, m_minorRadius);
        }
        return m_cachedMesh.get();
    }

    Engine::AABB boundingBox() const override {
        double outer = m_majorRadius + m_minorRadius;
        return Engine::AABB(
            m_center - Engine::Vec3(outer, m_minorRadius, outer),
            m_center + Engine::Vec3(outer, m_minorRadius, outer)
        );
    }

    void serialize(Core::OutputArchive& ar) const override {
        ar << m_center.x << m_center.y << m_center.z;
        ar << m_majorRadius << m_minorRadius;
    }

    void serialize(Core::InputArchive& ar) override {
        ar >> m_center.x >> m_center.y >> m_center.z;
        ar >> m_majorRadius >> m_minorRadius;
        invalidateCache();
    }

private:
    void invalidateCache() { m_cachedMesh.reset(); }

    Engine::Vec3 m_center{0.0};
    double m_majorRadius = 1.0;
    double m_minorRadius = 0.3;
    mutable std::unique_ptr<Engine::Mesh> m_cachedMesh;
};

} // namespace MulanGeo::Document
