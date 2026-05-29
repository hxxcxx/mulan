/**
 * @file SolidGeometryData.h
 * @brief OCCT Shape 几何数据 — BRepMesh 三角化 + 边线提取 + bounds
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../GeometryData.h"

// OCCT 前向声明
class TopoDS_Shape;

namespace mulan::world {

class SolidGeometryData : public GeometryData {
public:
    SolidGeometryData();
    ~SolidGeometryData() override;

    /// 从已有的 TopoDS_Shape 创建（移动语义）
    explicit SolidGeometryData(const TopoDS_Shape& shape);

    Type type() const override { return Type::Solid; }

    engine::Mesh faceMesh() const override;
    engine::Mesh edgeMesh() const override;
    engine::AABB bounds() const override;

    /// 设置新的 shape → 缓存失效
    void setShape(const TopoDS_Shape& shape);

private:
    engine::Mesh triangulate() const;
    engine::Mesh extractEdges() const;
    void invalidateMeshCache() const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mulan::world
