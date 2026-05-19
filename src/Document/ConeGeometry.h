/**
 * @file ConeGeometry.h
 * @brief 圆锥体几何体 — 参数化（底面中心 + 顶点 + 底面半径）
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

class DOCUMENT_API ConeGeometry : public Geometry {
    MULANGEO_OBJECT(ConeGeometry, Geometry)

public:
    ConeGeometry() = default;

    ConeGeometry(const Engine::Vec3& baseCenter, double radius, double height)
        : m_baseCenter(baseCenter), m_radius(radius), m_height(height) {}

    const Engine::Vec3& baseCenter() const { return m_baseCenter; }
    double radius() const { return m_radius; }
    double height() const { return m_height; }

    void setParameters(const Engine::Vec3& bc, double r, double h) {
        m_baseCenter = bc; m_radius = r; m_height = h; invalidateCache();
    }

    GeometryType geometryType() const override { return GeometryType::Cone; }

    const Engine::Mesh* displayMesh() const override {
        if (!m_cachedMesh) {
            m_cachedMesh = Engine::PrimitiveMesh::cone(m_radius, m_height);
        }
        return m_cachedMesh.get();
    }

    Engine::AABB boundingBox() const override {
        return Engine::AABB(
            Engine::Vec3(m_baseCenter.x - m_radius, m_baseCenter.y, m_baseCenter.z - m_radius),
            Engine::Vec3(m_baseCenter.x + m_radius, m_baseCenter.y + m_height, m_baseCenter.z + m_radius)
        );
    }

    void serialize(Core::OutputArchive& ar) const override {
        ar << m_baseCenter.x << m_baseCenter.y << m_baseCenter.z;
        ar << m_radius << m_height;
    }

    void serialize(Core::InputArchive& ar) override {
        ar >> m_baseCenter.x >> m_baseCenter.y >> m_baseCenter.z;
        ar >> m_radius >> m_height;
        invalidateCache();
    }

private:
    void invalidateCache() { m_cachedMesh.reset(); }

    Engine::Vec3 m_baseCenter{0.0};
    double m_radius = 0.5;
    double m_height = 1.0;
    mutable std::unique_ptr<Engine::Mesh> m_cachedMesh;
};

} // namespace MulanGeo::Document
