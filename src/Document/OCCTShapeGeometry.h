/**
 * @file OCCTShapeGeometry.h
 * @brief OCCT B-Rep 几何体 — 持有 TopoDS_Shape，延迟三角化
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 保留原始 OCCT shape，不立即三角化。
 * displayMesh() 在首次调用时三角化并缓存结果。
 */
#pragma once

#include "Geometry.h"

#include "MulanGeo/Engine/Geometry/Mesh.h"

#include <TopoDS_Shape.hxx>

#include <memory>
#include <mutex>

namespace MulanGeo::Document {

class DOCUMENT_API OCCTShapeGeometry : public Geometry {
public:
    explicit OCCTShapeGeometry(TopoDS_Shape shape);

    GeometryType geometryType() const override { return GeometryType::OCCTShape; }
    const char* typeName() const override { return "occt_shape"; }

    /// 原始 OCCT shape（只读）
    const TopoDS_Shape& shape() const { return m_shape; }

    /// 延迟三角化并缓存，线程安全
    const Engine::Mesh* displayMesh() const override;

    /// 延迟提取边线并缓存（从 OCCT TopAbs_EDGE 提取线段）
    const Engine::Mesh* edgeMesh() const override;

    /// 从 OCCT shape 计算包围盒
    Engine::AABB boundingBox() const override;

private:
    /// 执行三角化（内部调用）
    std::unique_ptr<Engine::Mesh> triangulate() const;

    /// 从 OCCT shape 提取边线（内部调用）
    std::unique_ptr<Engine::Mesh> extractEdges() const;

    TopoDS_Shape m_shape;
    mutable std::unique_ptr<Engine::Mesh> m_cachedMesh;
    mutable std::unique_ptr<Engine::Mesh> m_cachedEdgeMesh;
    mutable std::mutex m_cacheMutex;
    mutable bool m_meshGenerated = false;
    mutable bool m_edgeMeshGenerated = false;
};

} // namespace MulanGeo::Document
