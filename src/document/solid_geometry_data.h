/**
 * @file solid_geometry_data.h
 * @brief SolidGeometryData 将导入形体转换为可渲染网格和包围盒
 * @author hxxcxx
 * @date 2026-07-03
 */

#pragma once

#include <mulan/engine/geometry/mesh.h>
#include <mulan/engine/math/aabb.h>

#include <memory>

class TopoDS_Shape;

namespace mulan::document {

class SolidGeometryData {
public:
    SolidGeometryData();
    ~SolidGeometryData();

    explicit SolidGeometryData(const TopoDS_Shape& shape);

    engine::Mesh faceMesh() const;
    engine::Mesh edgeMesh() const;
    engine::AABB bounds() const;

    void setShape(const TopoDS_Shape& shape);

private:
    engine::Mesh triangulate() const;
    engine::Mesh extractEdges() const;
    void invalidateMeshCache() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mulan::document
