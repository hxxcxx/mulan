/**
 * @file SolidGeometryData.h
 * @brief OCCT Shape 几何数据 — BRepMesh 三角化 + 边线提取 + bounds
 * @author hxxcxx
 * @date 2026-05-29 (原始) / 2026-06-30 (迁移到 document 层)
 *
 * 从 world 层迁移至 document 层：B-Rep (TopoDS_Shape) 是真实数据源，
 * 由 Document 层持有。本类作为 world::GeometryData 的子类，负责把
 * B-Rep 惰性三角化为渲染网格。World 层不感知 OCCT。
 */

#pragma once

#include <mulan/world/GeometryData.h>

// OCCT 前向声明
class TopoDS_Shape;

namespace mulan::document {

class SolidGeometryData : public world::GeometryData {
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

} // namespace mulan::document
