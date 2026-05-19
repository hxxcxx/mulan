/**
 * @file SphereGeometry.h
 * @brief 球体几何体 — 参数化（中心 + 半径）
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

class DOCUMENT_API SphereGeometry : public Geometry {
    MULANGEO_OBJECT(SphereGeometry, Geometry)

public:
    SphereGeometry() = default;

    SphereGeometry(const Engine::Vec3& center, double radius)
        : m_center(center), m_radius(radius) {}

    const Engine::Vec3& center() const { return m_center; }
    double radius() const { return m_radius; }

    void setParameters(const Engine::Vec3& c, double r) {
        m_center = c; m_radius = r; invalidateCache();
    }

    GeometryType geometryType() const override { return GeometryType::Sphere; }

    const Engine::Mesh* displayMesh() const override {
        if (!m_cachedMesh) {
            m_cachedMesh = Engine::PrimitiveMesh::sphere(m_radius);
        }
        return m_cachedMesh.get();
    }

    Engine::AABB boundingBox() const override {
        return Engine::AABB(
            m_center - Engine::Vec3(m_radius),
            m_center + Engine::Vec3(m_radius)
        );
    }

    void serialize(Core::OutputArchive& ar) const override {
        ar << m_center.x << m_center.y << m_center.z << m_radius;
    }

    void serialize(Core::InputArchive& ar) override {
        ar >> m_center.x >> m_center.y >> m_center.z >> m_radius;
        invalidateCache();
    }

private:
    void invalidateCache() { m_cachedMesh.reset(); }

    Engine::Vec3 m_center{0.0};
    double m_radius = 0.5;
    mutable std::unique_ptr<Engine::Mesh> m_cachedMesh;
};

} // namespace MulanGeo::Document
