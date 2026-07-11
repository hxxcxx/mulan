/**
 * @file mesh_picking.h
 * @brief 网格拾取：三角形/线段的射线求交与候选收集。
 * @author hxxcxx
 * @date 2026-07-11
 */
#pragma once

#include "picking_types.h"

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <vector>

namespace mulan::view::detail {

/// 从网格顶点缓冲读取 Position 属性。
bool readPosition(const graphics::Mesh& mesh, uint32_t vertexIndex, math::Point3& out);

/// 从索引缓冲读取一个索引值。
bool readIndex(const graphics::Mesh& mesh, uint32_t indexIndex, uint32_t& out);

/// 读取第 triangleIndex 个三角形三个顶点位置。
bool readTriangle(const graphics::Mesh& mesh, uint32_t triangleIndex, math::Point3& v0, math::Point3& v1,
                  math::Point3& v2);

/// 读取第 segmentIndex 条线段两个端点位置。
bool readLineSegment(const graphics::Mesh& mesh, uint32_t segmentIndex, math::Point3& v0, math::Point3& v1);

/// 三角形网格射线拾取（最近命中）。
MeshPickResult pickTriangleMesh(const math::Ray3& worldRay, const graphics::Mesh& mesh,
                                const math::Mat4& worldTransform);

/// 线网格射线拾取（最近命中，带容差）。
MeshPickResult pickLineMesh(const math::Ray3& worldRay, const graphics::Mesh& mesh, const math::Mat4& worldTransform,
                            double lineToleranceWorld);

/// 按拓扑分派到三角形或线拾取。
MeshPickResult pickMesh(const math::Ray3& ray, const graphics::Mesh& mesh, const math::Mat4& worldTransform,
                        double lineToleranceWorld);

/// 三角形网格射线拾取（收集全部命中候选）。
void appendTriangleMeshPickCandidates(const math::Ray3& worldRay, const graphics::Mesh& mesh,
                                      const math::Mat4& worldTransform, std::vector<MeshPickResult>& out);

/// 线网格射线拾取（收集全部命中候选，带容差）。
void appendLineMeshPickCandidates(const math::Ray3& worldRay, const graphics::Mesh& mesh,
                                  const math::Mat4& worldTransform, double lineToleranceWorld,
                                  std::vector<MeshPickResult>& out);

/// 按拓扑分派收集全部命中候选。
void appendMeshPickCandidates(const math::Ray3& ray, const graphics::Mesh& mesh, const math::Mat4& worldTransform,
                              double lineToleranceWorld, std::vector<MeshPickResult>& out);

}  // namespace mulan::view::detail
