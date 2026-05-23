/**
 * @file Triangulation.h
 * @brief BRep 拓扑 → 三角网格 细化算法
 *
 * 基于 truck-meshalgo::tessellation 的 C++ 移植。
 * 将 Face/Shell/Solid 转化为三角形网格数据。
 *
 * 算法流程:
 *   1. 将 Edge 曲线离散为折线 (PolylineCurve)
 *   2. 将 3D 折线点投影到曲面的 (u,v) 参数空间
 *   3. 构建 2D 边界多边形 (PolyBoundary)
 *   4. 对曲面进行自适应参数分割生成内部填充点
 *   5. 约束 Delaunay 三角化 (CDT)
 *   6. 过滤边界外的三角形
 *   7. 生成顶点 (position + normal + uv)
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "../BRepExport.h"
#include "../Topology/Vertex.h"
#include "../Topology/Edge.h"
#include "../Topology/Wire.h"
#include "../Topology/Face.h"
#include "../Topology/Shell.h"
#include "../Topology/Solid.h"
#include "../CurveSurface/CurveSurface.h"
#include "../CurveSurface/CurveOps.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>
#include <MulanGeo/Geometry/Nurbs/NurbsCurve.h>
#include <MulanGeo/Geometry/Decorators/Processor.h>
#include <MulanGeo/Geometry/Decorators/RevolutedCurve.h>
#include <MulanGeo/Geometry/Decorators/ExtrudedCurve.h>
#include <MulanGeo/Geometry/Algo/curve/presearch.h>
#include <MulanGeo/Geometry/Algo/surface/search.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>
#include <utility>

namespace MulanGeo::BRep::tessellation {

using Geometry::Point3;
using Geometry::Vector3;
using Geometry::Vector2;
using Geometry::Matrix4;
using Geometry::ParameterRange;
using Geometry::Bound;
using Geometry::BoundKind;
using Geometry::near;
using Geometry::near2;
using Geometry::soSmall;

// ============================================================
// 折线 — 离散化的曲线
// ============================================================

struct PolylineCurve {
    std::vector<Point3> points;
    std::vector<double> params;

    static PolylineCurve fromCurve(const Curve& curve, double tol) {
        auto [t0, t1] = curve_rangeTuple(curve);
        return fromCurve(curve, {t0, t1}, tol);
    }

    static PolylineCurve fromCurve(const Curve& curve,
                                    std::pair<double, double> range, double tol) {
        PolylineCurve plc;
        auto [params, pts] = curve_parameterDivision(curve, range, tol);
        plc.params = std::move(params);
        plc.points = std::move(pts);
        return plc;
    }

    size_t size() const { return points.size(); }
    bool empty() const { return points.empty(); }
    const Point3& front() const { return points.front(); }
    const Point3& back() const { return points.back(); }
    bool isClosed(double tol = Geometry::TOLERANCE) const {
        return size() > 1 && near(front(), back(), tol);
    }
};

// ============================================================
// 2D 多边形边界 — 面在曲面参数空间中的边界
// ============================================================

struct PolyBoundary {
    struct Loop {
        std::vector<Vector2> uv_points;
        std::vector<Point3> points_3d;
        bool positive_orientation = true;

        double signedArea() const {
            double area = 0.0;
            size_t n = uv_points.size();
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                area += uv_points[i].x * uv_points[j].y;
                area -= uv_points[j].x * uv_points[i].y;
            }
            return area * 0.5;
        }

        bool isPositive() const { return signedArea() > 0.0; }
    };

    std::vector<Loop> loops;

    bool include(const Vector2& pt) const {
        bool inside = false;
        for (const auto& loop : loops) {
            if (pointInPolygon(pt, loop.uv_points)) {
                inside = !inside;
            }
        }
        return inside;
    }

    static bool pointInPolygon(const Vector2& pt, const std::vector<Vector2>& poly) {
        bool inside = false;
        size_t n = poly.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            double yi = poly[i].y, yj = poly[j].y;
            double xi = poly[i].x, xj = poly[j].x;
            if (((yi > pt.y) != (yj > pt.y)) &&
                (pt.x < (xj - xi) * (pt.y - yi) / (yj - yi) + xi)) {
                inside = !inside;
            }
        }
        return inside;
    }
};

// ============================================================
// 三角网格输出
// ============================================================

struct TriMesh {
    std::vector<Point3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<uint32_t> indices;

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return indices.size() / 3; }
    bool empty() const { return indices.empty(); }

    void merge(const TriMesh& other) {
        uint32_t offset = static_cast<uint32_t>(positions.size());
        positions.insert(positions.end(), other.positions.begin(), other.positions.end());
        normals.insert(normals.end(), other.normals.begin(), other.normals.end());
        uvs.insert(uvs.end(), other.uvs.begin(), other.uvs.begin() + other.uvs.size());
        for (auto idx : other.indices) {
            indices.push_back(idx + offset);
        }
    }

    void invertFaceWinding() {
        for (size_t i = 0; i < indices.size(); i += 3) {
            std::swap(indices[i + 1], indices[i + 2]);
        }
        for (auto& n : normals) {
            n = -n;
        }
    }
};

// ============================================================
// 辅助：将 3D 点投影到曲面参数空间
// ============================================================

inline std::optional<Vector2> projectToSurface(
    const Surface& surface, const Point3& point,
    const std::pair<double, double>& hint)
{
    using namespace Geometry::Algo::Surface;

    auto result = searchParameter(surface, point, hint);
    if (result) return Vector2(result->first, result->second);

    result = searchNearestParameter(surface, point, hint);
    if (result) return Vector2(result->first, result->second);

    return std::nullopt;
}

inline Vector2 projectToSurfaceRobust(
    const Surface& surface, const Point3& point,
    const std::pair<double, double>& hint)
{
    using namespace Geometry::Algo::Surface;

    auto result = searchParameter(surface, point, hint);
    if (result) return Vector2(result->first, result->second);

    result = searchNearestParameter(surface, point, hint);
    if (result) return Vector2(result->first, result->second);

    auto ranges = surface_parameterRange(surface);
    auto r0 = std::make_pair(
        boundsToDouble(ranges.first.first),
        boundsToDouble(ranges.first.second));
    auto r1 = std::make_pair(
        boundsToDouble(ranges.second.first),
        boundsToDouble(ranges.second.second));

    auto uv = presearch(surface, point, {r0, r1});
    result = searchNearestParameter(surface, point, uv);
    if (result) return Vector2(result->first, result->second);

    return Vector2(hint.first, hint.second);
}

// ============================================================
// 构建 Face 的 2D 参数空间边界
// ============================================================

inline PolyBoundary buildPolyBoundary(
    const Face<Point3, Curve, Surface>& face,
    double tol)
{
    PolyBoundary pb;

    for (size_t bi = 0; bi < face.numBoundaries(); ++bi) {
        const auto& wire = face.boundary(bi);
        PolyBoundary::Loop loop;

        auto surface_ranges = surface_parameterRange(face.surface());
        auto urange = std::make_pair(
            Geometry::Algo::Surface::boundsToDouble(surface_ranges.first.first),
            Geometry::Algo::Surface::boundsToDouble(surface_ranges.first.second));
        auto vrange = std::make_pair(
            Geometry::Algo::Surface::boundsToDouble(surface_ranges.second.first),
            Geometry::Algo::Surface::boundsToDouble(surface_ranges.second.second));

        double mid_u = (urange.first + urange.second) * 0.5;
        double mid_v = (vrange.first + vrange.second) * 0.5;
        Vector2 last_uv(mid_u, mid_v);

        for (size_t ei = 0; ei < wire.len(); ++ei) {
            const auto& edge = wire[ei];
            Curve oriented_curve = edge.orientedCurve();
            auto polyline = PolylineCurve::fromCurve(oriented_curve, tol);

            for (size_t pi = 0; pi < polyline.size(); ++pi) {
                const Point3& pt = polyline.points[pi];

                auto uv = projectToSurfaceRobust(face.surface(), pt,
                    std::make_pair(last_uv.x, last_uv.y));
                loop.uv_points.push_back(uv);
                loop.points_3d.push_back(pt);
                last_uv = uv;
            }
        }

        if (loop.uv_points.size() > 1 &&
            near(loop.uv_points.front(), loop.uv_points.back(), tol)) {
            loop.uv_points.pop_back();
            loop.points_3d.pop_back();
        }

        loop.positive_orientation = loop.isPositive();
        pb.loops.push_back(std::move(loop));
    }

    return pb;
}

// ============================================================
// 简化三角化：扇形 + 参数网格填充
// ============================================================

// ============================================================
// 简易约束三角化：耳切法边界 + 逐点插入内点
// ============================================================

/// 辅助：2D 点叉积 (z 分量)
inline double cross2(const Vector2& a, const Vector2& b) {
    return a.x * b.y - a.y * b.x;
}

/// 判断点 p 是否在三角形 (a,b,c) 内部 (UV 空间)
inline bool pointInTriangle(const Vector2& p, const Vector2& a,
                            const Vector2& b, const Vector2& c) {
    auto v0 = c - a, v1 = b - a, v2 = p - a;
    double dot00 = glm::dot(v0, v0);
    double dot01 = glm::dot(v0, v1);
    double dot02 = glm::dot(v0, v2);
    double dot11 = glm::dot(v1, v1);
    double dot12 = glm::dot(v1, v2);
    double inv = 1.0 / (dot00 * dot11 - dot01 * dot01);
    double u = (dot11 * dot02 - dot01 * dot12) * inv;
    double v = (dot00 * dot12 - dot01 * dot02) * inv;
    return u >= -1e-10 && v >= -1e-10 && u + v <= 1.0 + 1e-10;
}

/// 简易约束三角化结果
struct CDTResult {
    struct Tri {
        int v0, v1, v2;  // vertex indices
        double area;
    };
    std::vector<Vector2> vertices;        // UV coords
    std::vector<Tri> triangles;

    void addTriangle(int a, int b, int c) {
        double ar = std::abs(cross2(
            vertices[b] - vertices[a],
            vertices[c] - vertices[a])) * 0.5;
        triangles.push_back({a, b, c, ar});
    }

    /// 定位包含点 p 的三角形 (线性扫描)
    int locateTriangle(const Vector2& p) const {
        for (size_t i = 0; i < triangles.size(); ++i) {
            const auto& t = triangles[i];
            if (pointInTriangle(p, vertices[t.v0],
                                vertices[t.v1], vertices[t.v2]))
                return static_cast<int>(i);
        }
        return -1;
    }
};

/// 耳切法：将简单多边形 (ccw) 三角化
inline bool earClipPolygon(const std::vector<Vector2>& poly,
                           std::vector<std::array<int, 3>>& tris)
{
    size_t n = poly.size();
    if (n < 3) return false;
    if (n == 3) { tris.push_back({0, 1, 2}); return true; }

    // 判断顶点是否为凸耳
    auto isEar = [&](size_t i, size_t j, size_t k, const std::vector<bool>& removed) -> bool {
        Vector2 a = poly[i], b = poly[j], c = poly[k];
        // 检查凸性 (cross product z)
        double cross = cross2(b - a, c - b);
        if (cross <= 0.0) return false;  // 非凸或共线
        // 检查没有其他顶点在该三角形内部
        for (size_t m = 0; m < n; ++m) {
            if (m == i || m == j || m == k || removed[m]) continue;
            if (pointInTriangle(poly[m], a, b, c)) return false;
        }
        return true;
    };

    std::vector<bool> removed(n, false);
    size_t remaining = n;
    size_t prev = 0, ear_attempts = 0;

    while (remaining > 3) {
        // 找凸耳
        bool found = false;
        for (size_t start = prev; start < n + prev; ++start) {
            size_t i = start % n;
            if (removed[i]) continue;
            size_t j = (i + 1) % n;
            while (removed[j]) j = (j + 1) % n;
            size_t k = (j + 1) % n;
            while (removed[k]) k = (k + 1) % n;
            if (k == i) break;  // wrapped around

            if (isEar(i, j, k, removed)) {
                tris.push_back({static_cast<int>(i),
                                static_cast<int>(j),
                                static_cast<int>(k)});
                removed[j] = true;
                --remaining;
                prev = i;
                found = true;
                break;
            }
        }
        if (!found) {
            // 回退：强制切掉第一个可用顶点
            size_t i = prev;
            while (removed[i]) i = (i + 1) % n;
            size_t j = (i + 1) % n;
            while (removed[j]) j = (j + 1) % n;
            size_t k = (j + 1) % n;
            while (removed[k]) k = (k + 1) % n;
            if (k == i) break;
            tris.push_back({static_cast<int>(i),
                            static_cast<int>(j),
                            static_cast<int>(k)});
            removed[j] = true;
            --remaining;
            prev = i;
            ++ear_attempts;
            if (ear_attempts > n * 2) break;
        }
    }

    // 最后 3 个顶点组成一个三角形
    std::vector<int> last;
    for (size_t i = 0; i < n; ++i)
        if (!removed[i]) last.push_back(static_cast<int>(i));
    if (last.size() >= 3) {
        tris.push_back({last[0], last[1], last[2]});
    }

    return !tris.empty();
}

/// 构建边界约束三角化 (CDT approximated)
/// 1. 耳切法三角化边界多边形
/// 2. 逐点插入内部网格顶点
inline CDTResult buildConstrainedTriangulation(
    const PolyBoundary& pb,
    const std::vector<double>& u_div,
    const std::vector<double>& v_div)
{
    CDTResult cdt;

    // --- 步骤 1: 收集所有边界顶点 ---
    // 使用外环（第一个 loop）作为约束边界
    // 内孔暂做反转处理（耳切要求 CCW）
    if (pb.loops.empty()) return cdt;

    // 使用外环 (第一个 loop)
    const auto& outer = pb.loops[0];
    bool outer_ccw = outer.positive_orientation;  // signedArea > 0 表示 CCW

    std::vector<Vector2> boundary_pts = outer.uv_points;
    if (!outer_ccw) {
        std::reverse(boundary_pts.begin(), boundary_pts.end());
    }

    // 添加边界顶点到 CDT
    for (const auto& p : boundary_pts) {
        cdt.vertices.push_back(p);
    }

    // --- 步骤 2: 耳切法三角化边界 ---
    std::vector<std::array<int, 3>> boundary_tris;
    earClipPolygon(boundary_pts, boundary_tris);
    for (const auto& t : boundary_tris) {
        cdt.addTriangle(t[0], t[1], t[2]);
    }

    // --- 步骤 3: 插入内孔 — 带约束边 ---
    // 对每个内孔，插入孔的所有边作为约束边
    for (size_t li = 1; li < pb.loops.size(); ++li) {
        const auto& loop = pb.loops[li];

        // 确保 CCW 方向（耳切需要）
        std::vector<Vector2> hole_pts = loop.uv_points;
        bool hole_ccw = true;
        {
            double area = 0.0;
            size_t n = hole_pts.size();
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                area += hole_pts[i].x * hole_pts[j].y;
                area -= hole_pts[j].x * hole_pts[i].y;
            }
            hole_ccw = area > 0.0;
        }
        if (!hole_ccw) std::reverse(hole_pts.begin(), hole_pts.end());

        // 添加孔顶点（去重）
        std::vector<int> hole_vertex_indices;
        for (const auto& p : hole_pts) {
            // 查找已有顶点
            int found = -1;
            for (size_t vi = 0; vi < cdt.vertices.size(); ++vi) {
                if (near(cdt.vertices[vi].x, p.x) &&
                    near(cdt.vertices[vi].y, p.y)) {
                    found = static_cast<int>(vi);
                    break;
                }
            }
            if (found < 0) {
                found = static_cast<int>(cdt.vertices.size());
                cdt.vertices.push_back(p);
            }
            hole_vertex_indices.push_back(found);
        }

        // 用耳切法三角化孔洞本身（这些三角形会被后续移除）
        size_t hole_num = hole_vertex_indices.size();
        std::vector<std::array<int, 3>> hole_tris;
        earClipPolygon(hole_pts, hole_tris);

        // 逐点插入孔顶点到 CDT（标准点插入）
        for (int vi : hole_vertex_indices) {
            int tri_idx = cdt.locateTriangle(cdt.vertices[vi]);
            if (tri_idx >= 0) {
                const auto& tri = cdt.triangles[tri_idx];
                int t0 = tri.v0, t1 = tri.v1, t2 = tri.v2;
                cdt.triangles.erase(cdt.triangles.begin() + tri_idx);
                cdt.addTriangle(t0, t1, vi);
                cdt.addTriangle(t1, t2, vi);
                cdt.addTriangle(t2, t0, vi);
            }
        }

        // 翻转受影响的边以恢复约束边
        // 对于每条孔边 (hole[i], hole[i+1])，确保它在三角化中存在
        for (size_t ei = 0; ei < hole_num; ++ei) {
            int va = hole_vertex_indices[ei];
            int vb = hole_vertex_indices[(ei + 1) % hole_num];

            // 查找包含这条边的三角形
            bool edge_exists = false;
            for (const auto& tri : cdt.triangles) {
                int cnt = 0;
                if (tri.v0 == va || tri.v1 == va || tri.v2 == va) cnt++;
                if (tri.v0 == vb || tri.v1 == vb || tri.v2 == vb) cnt++;
                if (cnt == 2) { edge_exists = true; break; }
            }

            if (!edge_exists) {
                // 边不存在 → 需要翻转边
                // 找到共享 va 和 vb 的两个三角形对
                int tri_a = -1, tri_b = -1;
                for (size_t ti = 0; ti < cdt.triangles.size(); ++ti) {
                    const auto& tri = cdt.triangles[ti];
                    bool has_a = (tri.v0 == va || tri.v1 == va || tri.v2 == va);
                    bool has_b = (tri.v0 == vb || tri.v1 == vb || tri.v2 == vb);
                    if (has_a && has_b) {
                        if (tri_a < 0) tri_a = static_cast<int>(ti);
                        else { tri_b = static_cast<int>(ti); break; }
                    }
                }

                if (tri_a >= 0 && tri_b >= 0) {
                    // 边翻转: 找到四边形的对角顶点
                    const auto& ta = cdt.triangles[tri_a];
                    const auto& tb = cdt.triangles[tri_b];

                    // 找到 ta 中不是 va/vb 的顶点
                    int other_a = -1;
                    if (ta.v0 != va && ta.v0 != vb) other_a = ta.v0;
                    if (ta.v1 != va && ta.v1 != vb) other_a = ta.v1;
                    if (ta.v2 != va && ta.v2 != vb) other_a = ta.v2;

                    int other_b = -1;
                    if (tb.v0 != va && tb.v0 != vb) other_b = tb.v0;
                    if (tb.v1 != va && tb.v1 != vb) other_b = tb.v1;
                    if (tb.v2 != va && tb.v2 != vb) other_b = tb.v2;

                    if (other_a >= 0 && other_b >= 0) {
                        // 用 va-vb 边替换 other_a-other_b 边
                        // 移除旧的两个三角形，添加两个新的
                        int idx = static_cast<int>(tri_a);
                        cdt.triangles.erase(cdt.triangles.begin() + idx);
                        if (tri_b > idx) tri_b--;
                        cdt.triangles.erase(cdt.triangles.begin() + tri_b);
                        cdt.addTriangle(va, vb, other_a);
                        cdt.addTriangle(va, vb, other_b);
                    }
                }
            }
        }

        // 移除孔内部的三角形
        // 用孔的质心做点包含测试
        Vector2 hole_centroid(0.0);
        for (const auto& p : hole_pts) hole_centroid += p;
        hole_centroid /= static_cast<double>(hole_pts.size());

        // 也检查孔边界上的中点
        cdt.triangles.erase(
            std::remove_if(cdt.triangles.begin(), cdt.triangles.end(),
                [&](const CDTResult::Tri& tri) {
                    Vector2 centroid = (cdt.vertices[tri.v0] +
                                        cdt.vertices[tri.v1] +
                                        cdt.vertices[tri.v2]) / 3.0;
                    // 如果三角形质心在孔内部，移除
                    return PolyBoundary::pointInPolygon(centroid, hole_pts);
                }),
            cdt.triangles.end());
    }

    // --- 步骤 4: 插入内部网格顶点 ---
    for (double u : u_div) {
        for (double v : v_div) {
            Vector2 pt(u, v);
            // 检查是否在边界内
            if (!pb.include(pt)) continue;
            // 检查是否与已有顶点重复
            bool dup = false;
            for (const auto& vp : cdt.vertices) {
                if (near(vp.x, pt.x) && near(vp.y, pt.y)) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            int tri_idx = cdt.locateTriangle(pt);
            if (tri_idx < 0) continue;

            const auto& tri = cdt.triangles[tri_idx];
            int t0 = tri.v0, t1 = tri.v1, t2 = tri.v2;
            int new_v = static_cast<int>(cdt.vertices.size());
            cdt.vertices.push_back(pt);

            cdt.triangles.erase(cdt.triangles.begin() + tri_idx);
            cdt.addTriangle(t0, t1, new_v);
            cdt.addTriangle(t1, t2, new_v);
            cdt.addTriangle(t2, t0, new_v);
        }
    }

    return cdt;
}

inline TriMesh triangulateFace(
    const Face<Point3, Curve, Surface>& face,
    double tol)
{
    TriMesh mesh;

    Surface surface = face.surface();
    auto ranges = surface_parameterRange(surface);
    auto urange = std::make_pair(
        Geometry::Algo::Surface::boundsToDouble(ranges.first.first),
        Geometry::Algo::Surface::boundsToDouble(ranges.first.second));
    auto vrange = std::make_pair(
        Geometry::Algo::Surface::boundsToDouble(ranges.second.first),
        Geometry::Algo::Surface::boundsToDouble(ranges.second.second));

    PolyBoundary pb = buildPolyBoundary(face, tol);

    auto [u_div, v_div] = Geometry::Algo::Surface::parameterDivision(
        surface, {urange, vrange}, tol);

    // 构建约束三角化
    CDTResult cdt = buildConstrainedTriangulation(pb, u_div, v_div);

    if (cdt.triangles.empty()) return mesh;

    // 全局索引映射 (为了去重)
    struct VertInfo {
        Vector2 uv;
        Point3 pos;
        Vector3 normal;
    };
    std::vector<VertInfo> vert_infos;
    vert_infos.reserve(cdt.vertices.size());

    for (size_t i = 0; i < cdt.vertices.size(); ++i) {
        const auto& uv = cdt.vertices[i];
        Point3 pos = surface_subs(surface, uv.x, uv.y);
        Vector3 normal = surface_normal(surface, uv.x, uv.y);
        vert_infos.push_back({uv, pos, normal});
    }

    // 按 UV 去重（边界顶点可能在多个 loop 中出现）
    std::vector<int> cdt_to_mesh(cdt.vertices.size(), -1);
    auto findOrAddVertex = [&](int cdt_idx) -> int {
        if (cdt_to_mesh[cdt_idx] >= 0) return cdt_to_mesh[cdt_idx];
        const auto& vi = vert_infos[cdt_idx];
        // 检查 UV 是否已在 mesh 中
        for (size_t k = 0; k < mesh.uvs.size(); ++k) {
            if (near(mesh.uvs[k].x, vi.uv.x) &&
                near(mesh.uvs[k].y, vi.uv.y)) {
                cdt_to_mesh[cdt_idx] = static_cast<int>(k);
                return static_cast<int>(k);
            }
        }
        int idx = static_cast<int>(mesh.positions.size());
        mesh.positions.push_back(vi.pos);
        mesh.normals.push_back(vi.normal);
        mesh.uvs.push_back(vi.uv);
        cdt_to_mesh[cdt_idx] = idx;
        return idx;
    };

    for (const auto& tri : cdt.triangles) {
        if (tri.area < 1e-20) continue;
        int a = findOrAddVertex(tri.v0);
        int b = findOrAddVertex(tri.v1);
        int c_c = findOrAddVertex(tri.v2);
        Vector3 e1 = mesh.positions[b] - mesh.positions[a];
        Vector3 e2 = mesh.positions[c_c] - mesh.positions[a];
        Vector3 cross = glm::cross(e1, e2);
        if (glm::dot(cross, mesh.normals[a]) < 0) {
            std::swap(b, c_c);
        }
        mesh.indices.push_back(static_cast<uint32_t>(a));
        mesh.indices.push_back(static_cast<uint32_t>(b));
        mesh.indices.push_back(static_cast<uint32_t>(c_c));
    }

    if (!face.orientation()) {
        mesh.invertFaceWinding();
    }

    return mesh;
}

// ============================================================
// Shell / Solid 细化
// ============================================================

inline TriMesh triangulateShell(
    const Shell<Point3, Curve, Surface>& shell,
    double tol)
{
    TriMesh mesh;
    for (size_t i = 0; i < shell.len(); ++i) {
        auto face_mesh = triangulateFace(shell[i], tol);
        mesh.merge(face_mesh);
    }
    return mesh;
}

inline std::vector<TriMesh> triangulateSolid(
    const Solid<Point3, Curve, Surface>& solid,
    double tol)
{
    std::vector<TriMesh> meshes;
    for (size_t i = 0; i < solid.boundaries().size(); ++i) {
        meshes.push_back(triangulateShell(solid.boundaries()[i], tol));
    }
    return meshes;
}

inline TriMesh triangulateSolidMerged(
    const Solid<Point3, Curve, Surface>& solid,
    double tol)
{
    TriMesh mesh;
    for (size_t i = 0; i < solid.boundaries().size(); ++i) {
        mesh.merge(triangulateShell(solid.boundaries()[i], tol));
    }
    return mesh;
}

} // namespace MulanGeo::BRep::tessellation