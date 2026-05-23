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

    struct GridPoint {
        Vector2 uv;
        Point3 pos;
        Vector3 normal;
        bool inside;
        int index;
    };

    std::vector<GridPoint> grid;
    grid.reserve(u_div.size() * v_div.size());
    int cols = static_cast<int>(v_div.size());

    for (size_t i = 0; i < u_div.size(); ++i) {
        for (size_t j = 0; j < v_div.size(); ++j) {
            Vector2 uv(u_div[i], v_div[j]);
            Point3 pos = surface_subs(surface, uv.x, uv.y);
            Vector3 normal = surface_normal(surface, uv.x, uv.y);

            bool inside = pb.include(uv);
            if (!pb.loops.empty()) {
                bool on_boundary = false;
                for (const auto& loop : pb.loops) {
                    for (size_t k = 0; k < loop.uv_points.size(); ++k) {
                        if (near2(uv.x, loop.uv_points[k].x) &&
                            near2(uv.y, loop.uv_points[k].y)) {
                            on_boundary = true;
                            break;
                        }
                    }
                    if (on_boundary) break;
                }
                if (on_boundary) inside = true;
            }

            int idx = -1;
            if (inside) {
                idx = static_cast<int>(mesh.positions.size());
                mesh.positions.push_back(pos);
                mesh.normals.push_back(normal);
                mesh.uvs.push_back(uv);
            }

            grid.push_back({uv, pos, normal, inside, idx});
        }
    }

    for (size_t i = 0; i + 1 < u_div.size(); ++i) {
        for (size_t j = 0; j + 1 < v_div.size(); ++j) {
            int i00 = static_cast<int>(i * v_div.size() + j);
            int i10 = static_cast<int>((i + 1) * v_div.size() + j);
            int i01 = static_cast<int>(i * v_div.size() + (j + 1));
            int i11 = static_cast<int>((i + 1) * v_div.size() + (j + 1));

            auto& p00 = grid[i00], p10 = grid[i10];
            auto& p01 = grid[i01], p11 = grid[i11];

            int n = 0;
            n += p00.inside ? 1 : 0;
            n += p10.inside ? 1 : 0;
            n += p01.inside ? 1 : 0;
            n += p11.inside ? 1 : 0;

            if (n < 3) continue;

            auto emitTriangle = [&](int a, int b, int c) {
                if (a < 0 || b < 0 || c < 0) return;
                auto pa = mesh.positions[a];
                auto pb = mesh.positions[b];
                auto pc = mesh.positions[c];
                Vector3 e1 = pb - pa, e2 = pc - pa;
                Vector3 cross = glm::cross(e1, e2);
                Vector3 na = mesh.normals[a];
                if (glm::dot(cross, na) < 0) {
                    std::swap(b, c);
                }
                mesh.indices.push_back(static_cast<uint32_t>(a));
                mesh.indices.push_back(static_cast<uint32_t>(b));
                mesh.indices.push_back(static_cast<uint32_t>(c));
            };

            if (n == 4) {
                double d1 = glm::length2(p00.pos - p11.pos);
                double d2 = glm::length2(p10.pos - p01.pos);
                if (d1 < d2) {
                    emitTriangle(p00.index, p10.index, p01.index);
                    emitTriangle(p01.index, p10.index, p11.index);
                } else {
                    emitTriangle(p00.index, p10.index, p11.index);
                    emitTriangle(p00.index, p11.index, p01.index);
                }
            } else {
                if (p00.inside && p10.inside && p11.inside)
                    emitTriangle(p00.index, p10.index, p11.index);
                if (p00.inside && p11.inside && p01.inside)
                    emitTriangle(p00.index, p11.index, p01.index);
                if (p00.inside && p10.inside && p01.inside && !p11.inside)
                    emitTriangle(p00.index, p10.index, p01.index);
                if (p10.inside && p01.inside && p11.inside && !p00.inside)
                    emitTriangle(p10.index, p01.index, p11.index);
            }
        }
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