/**
 * @file BoxGeometry.h
 * @brief 长方体几何体 — 参数化几何体的参考模板
 * @author hxxcxx
 * @date 2026-05-19
 *
 * 设计要点：
 *   - 只存 3 个参数 (dx, dy, dz)，文件极小
 *   - displayMesh() 按需调用 PrimitiveMesh::box() 生成网格并缓存
 *   - 序列化只写 3 个 double
 *   - 未来加新参数化类型（如 ExtrusionGeometry）照抄此模式
 */
#pragma once

#include "DocumentExport.h"
#include "Geometry.h"

#include "mulan/engine/geometry/PrimitiveMesh.h"
#include "mulan/core/serialization/Archive.h"

#include <memory>

namespace mulan::document {

class DOCUMENT_API BoxGeometry : public Geometry {
    MULANGEO_OBJECT(BoxGeometry, Geometry)

public:
    BoxGeometry() = default;

    BoxGeometry(double dx, double dy, double dz)
        : m_dx(dx), m_dy(dy), m_dz(dz) {}

    // --- 参数访问/修改 ---

    double dx() const { return m_dx; }
    double dy() const { return m_dy; }
    double dz() const { return m_dz; }

    void setDimensions(double dx, double dy, double dz) {
        m_dx = dx; m_dy = dy; m_dz = dz;
        invalidateCache();
    }

    // --- Geometry 接口 ---

    GeometryType geometryType() const override { return GeometryType::Box; }

    const engine::Mesh* displayMesh() const override {
        ensureMesh();
        return m_cachedMesh.get();
    }

    engine::AABB boundingBox() const override {
        return engine::AABB(
            Vec3(-m_dx * 0.5, -m_dy * 0.5, -m_dz * 0.5),
            Vec3( m_dx * 0.5,  m_dy * 0.5,  m_dz * 0.5)
        );
    }

    // --- core::Object 序列化（只写 3 个 double）---

    void serialize(core::OutputArchive& ar) const override {
        ar << m_dx << m_dy << m_dz;
    }

    void serialize(core::InputArchive& ar) override {
        ar >> m_dx >> m_dy >> m_dz;
        invalidateCache();
    }

private:
    void ensureMesh() const {
        if (!m_cachedMesh) {
            m_cachedMesh = engine::PrimitiveMesh::box(m_dx, m_dy, m_dz);
        }
    }

    void invalidateCache() {
        m_cachedMesh.reset();
    }

    // ---- 参数（只存这几个 double！）----
    double m_dx = 1.0;
    double m_dy = 1.0;
    double m_dz = 1.0;

    // ---- 缓存（不序列化，按需生成）----
    mutable std::unique_ptr<engine::Mesh> m_cachedMesh;
};

} // namespace mulan::document
