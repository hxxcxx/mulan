/**
 * @file CylinderGeometry.h
 * @brief 圆柱体几何体 — 参数化（起点+终点+半径）
 * @author hxxcxx
 * @date 2026-05-19
 *
 * 参照老系统 CylinderSolidEntity 设计：
 *   存 startPos, endPos, radius → 渲染时生成 Mesh
 */
#pragma once

#include "DocumentExport.h"
#include "Geometry.h"

#include "mulan/Engine/Geometry/PrimitiveMesh.h"
#include "mulan/Core/Serialization/Archive.h"

#include <memory>

namespace mulan::document {

class DOCUMENT_API CylinderGeometry : public Geometry {
    MULANGEO_OBJECT(CylinderGeometry, Geometry)

public:
    CylinderGeometry() = default;

    CylinderGeometry(const engine::Vec3& startPos,
                     const engine::Vec3& endPos,
                     double radius)
        : m_startPos(startPos), m_endPos(endPos), m_radius(radius) {}

    // --- 参数 ---

    const engine::Vec3& startPosition() const { return m_startPos; }
    const engine::Vec3& endPosition() const { return m_endPos; }
    double radius() const { return m_radius; }

    void setParameters(const engine::Vec3& start, const engine::Vec3& end, double r) {
        m_startPos = start; m_endPos = end; m_radius = r;
        invalidateCache();
    }

    // --- Geometry 接口 ---

    GeometryType geometryType() const override { return GeometryType::Cylinder; }

    const engine::Mesh* displayMesh() const override {
        ensureMesh();
        return m_cachedMesh.get();
    }

    engine::AABB boundingBox() const override {
        // 简化：用两端点 + 半径的 AABB 包围
        double r = m_radius;
        engine::Vec3 mn(
            std::min(m_startPos.x, m_endPos.x) - r,
            std::min(m_startPos.y, m_endPos.y) - r,
            std::min(m_startPos.z, m_endPos.z) - r
        );
        engine::Vec3 mx(
            std::max(m_startPos.x, m_endPos.x) + r,
            std::max(m_startPos.y, m_endPos.y) + r,
            std::max(m_startPos.z, m_endPos.z) + r
        );
        return engine::AABB(mn, mx);
    }

    // --- 序列化（只存 7 个 double）---

    void serialize(core::OutputArchive& ar) const override {
        ar << m_startPos.x << m_startPos.y << m_startPos.z;
        ar << m_endPos.x   << m_endPos.y   << m_endPos.z;
        ar << m_radius;
    }

    void serialize(core::InputArchive& ar) override {
        ar >> m_startPos.x >> m_startPos.y >> m_startPos.z;
        ar >> m_endPos.x   >> m_endPos.y   >> m_endPos.z;
        ar >> m_radius;
        invalidateCache();
    }

private:
    void ensureMesh() const {
        if (!m_cachedMesh) {
            double height = glm::length(m_endPos - m_startPos);
            auto baseMesh = engine::PrimitiveMesh::cylinder(m_radius, height);
            // 旋转 + 平移使圆柱从 startPos 到 endPos
            m_cachedMesh = std::make_unique<engine::Mesh>();
            *m_cachedMesh = std::move(*baseMesh);
            // TODO: 应用变换矩阵到顶点
        }
    }

    void invalidateCache() { m_cachedMesh.reset(); }

    engine::Vec3 m_startPos{0.0};
    engine::Vec3 m_endPos{0.0, 1.0, 0.0};
    double m_radius = 0.5;

    mutable std::unique_ptr<engine::Mesh> m_cachedMesh;
};

} // namespace mulan::Document
