/**
 * @file BVH.h
 * @brief 轴对齐包围盒 (AABB) 层次结构 — 空间加速碰撞检测
 *
 * 基于 truck-meshalgo::analyzers::collision 的排序端点扫描法。
 * 将三角形投影到主轴上排序，通过区间重叠快速筛选候选对，
 * 再做精确的三角形-三角形相交测试。
 *
 * 复杂度：O((n+m) log(n+m) + k) 其中 k 为实际相交对数
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "Triangulation.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <array>
#include <utility>

namespace MulanGeo::BRep::tessellation {

using Geometry::Point3;
using Geometry::Vector3;
using Geometry::near;
using Geometry::soSmall;
using Geometry::TOLERANCE;

// ============================================================
// AABB 包围盒
// ============================================================

struct AABB {
    Point3 min_pt{std::numeric_limits<double>::max()};
    Point3 max_pt{std::numeric_limits<double>::lowest()};

    void expand(const Point3& p) {
        min_pt = glm::min(min_pt, p);
        max_pt = glm::max(max_pt, p);
    }

    bool overlaps(const AABB& other, double tol = 0.0) const {
        return min_pt.x - tol <= other.max_pt.x &&
               other.min_pt.x - tol <= max_pt.x &&
               min_pt.y - tol <= other.max_pt.y &&
               other.min_pt.y - tol <= max_pt.y &&
               min_pt.z - tol <= other.max_pt.z &&
               other.min_pt.z - tol <= max_pt.z;
    }

    Point3 center() const { return (min_pt + max_pt) * 0.5; }

    static AABB fromTriangle(const Triangle& tri) {
        AABB bb;
        for (int i = 0; i < 3; ++i) bb.expand(tri.v[i]);
        return bb;
    }
};

// ============================================================
// 排序端点扫描法 — O((n+m)log(n+m)) 碰撞候选对筛选
// ============================================================

/// 端点类型：区间起点或终点
enum class EndpointType : uint8_t {
    Start = 0,
    End = 1,
};

/// 投影端点
struct Endpoint {
    double proj;       // 沿主轴的投影坐标
    EndpointType type; // 起点或终点
    uint8_t mesh_id;   // 所属网格 (0 或 1)
    uint32_t tri_idx;  // 三角形索引
};

/// 从 TriMesh 提取三角形并计算 AABB
inline void extractTriangles(
    const TriMesh& mesh,
    std::vector<Triangle>& tris,
    std::vector<AABB>& aabbs)
{
    tris.resize(mesh.triangleCount());
    aabbs.resize(mesh.triangleCount());
    for (size_t i = 0; i < mesh.triangleCount(); ++i) {
        uint32_t i0 = mesh.indices[i * 3 + 0];
        uint32_t i1 = mesh.indices[i * 3 + 1];
        uint32_t i2 = mesh.indices[i * 3 + 2];
        tris[i].v[0] = mesh.positions[i0];
        tris[i].v[1] = mesh.positions[i1];
        tris[i].v[2] = mesh.positions[i2];
        aabbs[i] = AABB::fromTriangle(tris[i]);
    }
}

/// 选择最优投影轴（最大伸展方向）
inline int selectAxis(const std::vector<AABB>& aabbs0, const std::vector<AABB>& aabbs1) {
    Vector3 extent(0.0);
    for (const auto& bb : aabbs0) extent += bb.max_pt - bb.min_pt;
    for (const auto& bb : aabbs1) extent += bb.max_pt - bb.min_pt;

    if (extent.x >= extent.y && extent.x >= extent.z) return 0;
    if (extent.y >= extent.z) return 1;
    return 2;
}

/// 排序端点扫描法提取碰撞候选对
inline std::vector<std::pair<uint32_t, uint32_t>> sweepAndPrune(
    const std::vector<AABB>& aabbs0,
    const std::vector<AABB>& aabbs1,
    int axis)
{
    // 构建端点列表
    std::vector<Endpoint> endpoints;
    endpoints.reserve((aabbs0.size() + aabbs1.size()) * 2);

    for (size_t i = 0; i < aabbs0.size(); ++i) {
        double lo = aabbs0[i].min_pt[axis];
        double hi = aabbs0[i].max_pt[axis];
        endpoints.push_back({lo, EndpointType::Start, 0, static_cast<uint32_t>(i)});
        endpoints.push_back({hi, EndpointType::End, 0, static_cast<uint32_t>(i)});
    }
    for (size_t i = 0; i < aabbs1.size(); ++i) {
        double lo = aabbs1[i].min_pt[axis];
        double hi = aabbs1[i].max_pt[axis];
        endpoints.push_back({lo, EndpointType::Start, 1, static_cast<uint32_t>(i)});
        endpoints.push_back({hi, EndpointType::End, 1, static_cast<uint32_t>(i)});
    }

    // 按投影坐标排序
    std::sort(endpoints.begin(), endpoints.end(),
        [](const Endpoint& a, const Endpoint& b) {
            if (a.proj != b.proj) return a.proj < b.proj;
            // 起点优先于终点
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        });

    // 扫描：维护活跃三角形集合
    std::vector<uint32_t> active0, active1;
    std::vector<std::pair<uint32_t, uint32_t>> candidates;

    for (const auto& ep : endpoints) {
        if (ep.type == EndpointType::Start) {
            if (ep.mesh_id == 0) {
                // 与 mesh1 的所有活跃三角形配对
                for (uint32_t j : active1) {
                    if (aabbs0[ep.tri_idx].overlaps(aabbs1[j])) {
                        candidates.emplace_back(ep.tri_idx, j);
                    }
                }
                active0.push_back(ep.tri_idx);
            } else {
                // 与 mesh0 的所有活跃三角形配对
                for (uint32_t j : active0) {
                    if (aabbs1[ep.tri_idx].overlaps(aabbs0[j])) {
                        candidates.emplace_back(j, ep.tri_idx);
                    }
                }
                active1.push_back(ep.tri_idx);
            }
        } else {
            // 移除活跃三角形
            auto& active = (ep.mesh_id == 0) ? active0 : active1;
            auto it = std::find(active.begin(), active.end(), ep.tri_idx);
            if (it != active.end()) {
                *it = active.back();
                active.pop_back();
            }
        }
    }

    return candidates;
}

// ============================================================
// 加速版碰撞检测
// ============================================================

/// 使用 AABB 排序端点扫描法的加速版干涉线段提取
inline std::vector<LineSegment> extractInterferenceAccelerated(
    const TriMesh& mesh0, const TriMesh& mesh1)
{
    // 提取三角形和包围盒
    std::vector<Triangle> tris0, tris1;
    std::vector<AABB> aabbs0, aabbs1;
    extractTriangles(mesh0, tris0, aabbs0);
    extractTriangles(mesh1, tris1, aabbs1);

    if (tris0.empty() || tris1.empty()) return {};

    // 选择投影轴
    int axis = selectAxis(aabbs0, aabbs1);

    // 扫描法获取候选对
    auto candidates = sweepAndPrune(aabbs0, aabbs1, axis);

    // 精确三角形-三角形相交测试
    std::vector<LineSegment> segments;
    segments.reserve(candidates.size());
    for (const auto& [idx0, idx1] : candidates) {
        LineSegment seg;
        if (triangleTriangleIntersection(tris0[idx0], tris1[idx1], seg)) {
            segments.push_back(seg);
        }
    }

    return segments;
}

// ============================================================
// BVH 节点 — 用于单网格内的空间查询
// ============================================================

struct BVHNode {
    AABB bounds;
    int left = -1;   // 左子节点索引（-1 表示叶子）
    int right = -1;  // 右子节点索引
    uint32_t start = 0; // 叶子节点：起始三角形索引
    uint32_t count = 0; // 叶子节点：三角形数量
};

/// 简单 BVH 构建（中点分割）
class BVH {
public:
    BVH() = default;

    /// 从三角形列表构建 BVH
    void build(const std::vector<Triangle>& triangles) {
        if (triangles.empty()) return;

        // 计算所有 AABB
        triangle_aabbs_.resize(triangles.size());
        for (size_t i = 0; i < triangles.size(); ++i) {
            triangle_aabbs_[i] = AABB::fromTriangle(triangles[i]);
        }

        // 初始索引列表
        std::vector<uint32_t> indices(triangles.size());
        for (size_t i = 0; i < triangles.size(); ++i) indices[i] = static_cast<uint32_t>(i);

        // 递归构建
        nodes_.reserve(triangles.size() * 2);
        buildRecursive(indices, 0, static_cast<uint32_t>(indices.size()), 0);
    }

    /// 查询与 AABB 相交的所有三角形索引
    void query(const AABB& query_box, std::vector<uint32_t>& results) const {
        if (nodes_.empty()) return;
        queryRecursive(0, query_box, results);
    }

    /// 查询与线段相交的所有三角形索引（用于射线检测）
    void queryRay(const Point3& origin, const Vector3& dir,
                  std::vector<uint32_t>& results) const {
        if (nodes_.empty()) return;
        queryRayRecursive(0, origin, dir, results);
    }

    const std::vector<AABB>& triangleAABBs() const { return triangle_aabbs_; }

private:
    std::vector<BVHNode> nodes_;
    std::vector<AABB> triangle_aabbs_;

    uint32_t buildRecursive(std::vector<uint32_t>& indices,
                            uint32_t start, uint32_t end, int depth)
    {
        uint32_t node_idx = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(BVHNode{});

        // 计算包围盒
        AABB bounds;
        for (uint32_t i = start; i < end; ++i) {
            bounds.expand(triangle_aabbs_[indices[i]].min_pt);
            bounds.expand(triangle_aabbs_[indices[i]].max_pt);
        }
        nodes_[node_idx].bounds = bounds;

        uint32_t count = end - start;

        // 叶子节点条件
        if (count <= 4 || depth > 20) {
            nodes_[node_idx].start = start;
            nodes_[node_idx].count = count;
            // 排序索引到连续段
            // 在原数组中已经是连续的
            return node_idx;
        }

        // 选择最长轴分割
        Vector3 extent = bounds.max_pt - bounds.min_pt;
        int axis = 0;
        if (extent.y > extent.x) axis = 1;
        if (extent.z > extent[static_cast<size_t>(axis)]) axis = 2;

        double mid = (bounds.min_pt[axis] + bounds.max_pt[axis]) * 0.5;

        // 按中点分割
        auto mid_iter = std::partition(
            indices.begin() + start,
            indices.begin() + end,
            [this, axis, mid](uint32_t idx) {
                return triangle_aabbs_[idx].center()[axis] < mid;
            });

        uint32_t mid_pos = static_cast<uint32_t>(mid_iter - indices.begin());

        // 防止退化（所有元素在同一侧）
        if (mid_pos == start || mid_pos == end) {
            mid_pos = start + count / 2;
        }

        nodes_[node_idx].left = static_cast<int>(buildRecursive(indices, start, mid_pos, depth + 1));
        nodes_[node_idx].right = static_cast<int>(buildRecursive(indices, mid_pos, end, depth + 1));

        return node_idx;
    }

    void queryRecursive(uint32_t node_idx, const AABB& query_box,
                        std::vector<uint32_t>& results) const
    {
        const auto& node = nodes_[node_idx];
        if (!node.bounds.overlaps(query_box)) return;

        if (node.count > 0) {
            // 叶子节点
            for (uint32_t i = 0; i < node.count; ++i) {
                uint32_t tri_idx = node.start + i; // 简化：使用起始索引
                if (triangle_aabbs_[tri_idx].overlaps(query_box)) {
                    results.push_back(tri_idx);
                }
            }
        } else {
            if (node.left >= 0) queryRecursive(static_cast<uint32_t>(node.left), query_box, results);
            if (node.right >= 0) queryRecursive(static_cast<uint32_t>(node.right), query_box, results);
        }
    }

    void queryRayRecursive(uint32_t node_idx, const Point3& origin,
                           const Vector3& dir, std::vector<uint32_t>& results) const
    {
        const auto& node = nodes_[node_idx];

        // 简化 AABB-射线相交测试
        if (!rayAABBIntersect(origin, dir, node.bounds)) return;

        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) {
                results.push_back(node.start + i);
            }
        } else {
            if (node.left >= 0) queryRayRecursive(static_cast<uint32_t>(node.left), origin, dir, results);
            if (node.right >= 0) queryRayRecursive(static_cast<uint32_t>(node.right), origin, dir, results);
        }
    }

    static bool rayAABBIntersect(const Point3& origin, const Vector3& dir,
                                  const AABB& box, double tmin = 0.0,
                                  double tmax = 1e18)
    {
        for (int i = 0; i < 3; ++i) {
            double inv_d = (std::abs(dir[i]) < 1e-15) ?
                1e15 * ((dir[i] >= 0) ? 1.0 : -1.0) : 1.0 / dir[i];
            double t0 = (box.min_pt[i] - origin[i]) * inv_d;
            double t1 = (box.max_pt[i] - origin[i]) * inv_d;
            if (inv_d < 0.0) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmax < tmin) return false;
        }
        return true;
    }
};

} // namespace MulanGeo::BRep::tessellation
