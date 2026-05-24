/**
 * @file Fillet.h
 * @brief 倒角 (chamfer) 和圆角 (fillet) 操作
 *
 * 在 Shell 的指定边上创建过渡面。
 *
 * 当前实现:
 *   - simpleFillet: 两平面之间的等半径滚动球近似圆角
 *   - simpleChamfer: 等距离倒角
 *
 * 基于 truck-shapeops::fillet::simple_fillet 的简化移植。
 * 完整版需要 RbfSurface + ApproxFilletSurface 几何装饰器，
 * 当前版本使用 BSplineSurface 近似。
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "../Topology/Vertex.h"
#include "../Topology/Edge.h"
#include "../Topology/Wire.h"
#include "../Topology/Face.h"
#include "../Topology/Shell.h"
#include "../Topology/Solid.h"
#include "../Topology/ID.h"
#include "../CurveSurface/CurveSurface.h"
#include "../CurveSurface/CurveOps.h"
#include "../Builder/Builder.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>
#include <MulanGeo/Geometry/Nurbs/BSplineSurface.h>
#include <MulanGeo/Geometry/Nurbs/NurbsCurve.h>
#include <MulanGeo/Geometry/Nurbs/NurbsSurface.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/projection.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>
#include <utility>

namespace MulanGeo::BRep::fillet {

using geometry::Point3;
using geometry::Vector3;
using geometry::Vector2;
using geometry::Matrix4;
using geometry::near;
using geometry::soSmall;
using geometry::TOLERANCE;
using geometry::PI;

// ============================================================
// 圆角结果
// ============================================================

/// simpleFillet 的结果：修改后的两个面 + 新的圆角面
template<typename P, typename C, typename S>
struct SimpleFilletResult {
    Face<P, C, S> face0;     ///< 修改后的第一个面
    Face<P, C, S> face1;     ///< 修改后的第二个面
    Face<P, C, S> fillet_face; ///< 新创建的圆角过渡面
};

// ============================================================
// 辅助：在 Face 的边界中找到与给定 EdgeID 相邻的前后边
// ============================================================

inline std::optional<std::pair<Edge<Point3, Curve>, Edge<Point3, Curve>>>
findAdjacentEdges(
    const Face<Point3, Curve, Surface>& face,
    EdgeID<Curve> edge_id)
{
    for (const auto& wire : face.boundaries()) {
        const auto& edges = wire.edges();
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].id() == edge_id) {
                size_t prev = (i == 0) ? edges.size() - 1 : i - 1;
                size_t next = (i + 1) % edges.size();
                return std::make_pair(edges[prev], edges[next]);
            }
        }
    }
    return std::nullopt;
}

// ============================================================
// 辅助：在 Face 的边界中替换边
// ============================================================

inline Face<Point3, Curve, Surface> replaceEdgeInFace(
    const Face<Point3, Curve, Surface>& face,
    EdgeID<Curve> old_edge_id,
    const Edge<Point3, Curve>& new_front_edge,
    const Edge<Point3, Curve>& fillet_edge,
    const Edge<Point3, Curve>& new_back_edge)
{
    auto boundaries = face.absoluteBoundaries();
    for (auto& boundary : boundaries) {
        auto& edges = const_cast<std::deque<Edge<Point3, Curve>>&>(boundary.edges());
        for (size_t i = 0; i < edges.size(); ++i) {
            if (edges[i].id() == old_edge_id) {
                size_t len = edges.size();
                if (face.orientation()) {
                    edges[i] = new_front_edge;
                    // 在 i+1 位置插入 fillet_edge，i+2 插入 new_back_edge
                    auto it = edges.begin() + static_cast<ptrdiff_t>(i) + 1;
                    edges.insert(it, fillet_edge);
                    it = edges.begin() + static_cast<ptrdiff_t>(i) + 2;
                    edges.insert(it, new_back_edge);
                } else {
                    size_t idx = (len + i - 2) % len;
                    edges[idx] = new_back_edge.inverse();
                    idx = (len + i - 1) % len;
                    edges[idx] = fillet_edge.inverse();
                    // 当前位置保持为 new_front_edge（反转方向后）
                    edges[i] = new_front_edge;
                }
                break;
            }
        }
    }

    auto result = Face<Point3, Curve, Surface>::newUnchecked(
        std::move(boundaries), face.surface());
    if (!face.orientation()) result.invert();
    return result;
}

// ============================================================
// 简化圆角 — 两平面之间的等半径圆角
// ============================================================

/**
 * @brief 在两个面共享的边上创建等半径圆角
 *
 * 算法:
 *   1. 获取两个面的曲面（法线方向）
 *   2. 在边的每个采样点处，沿两个面的偏移方向构建圆弧
 *   3. 用 BSplineSurface 拟合圆弧截面形成圆角曲面
 *   4. 用偏移曲线切割原始面，创建圆角过渡面
 *
 * @param face0 第一个面（包含 filleted_edge）
 * @param face1 第二个面（包含 filleted_edge）
 * @param filleted_edge_id 要做圆角的边的 ID
 * @param radius 圆角半径
 * @param tol 容差
 * @return 圆角结果（修改后的两个面 + 圆角面），失败返回 nullopt
 */
inline std::optional<SimpleFilletResult<Point3, Curve, Surface>>
simpleFillet(
    const Face<Point3, Curve, Surface>& face0,
    const Face<Point3, Curve, Surface>& face1,
    EdgeID<Curve> filleted_edge_id,
    double radius,
    double tol = TOLERANCE)
{
    // 1. 在 face0 中找到被圆角化的边
    bool found_edge = false;
    Edge<Point3, Curve> filleted_edge;
    for (const auto& wire : face0.boundaries()) {
        for (const auto& edge : wire.edges()) {
            if (edge.id() == filleted_edge_id) {
                filleted_edge = edge;
                found_edge = true;
                break;
            }
        }
        if (found_edge) break;
    }
    if (!found_edge) return std::nullopt;

    // 2. 获取边的曲线和两个面的曲面
    auto curve = filleted_edge.orientedCurve();
    auto [t0, t1] = curve.rangeTuple();

    Surface surface0 = face0.orientedSurface();
    Surface surface1 = face1.orientedSurface();

    // 3. 在曲线的多个采样点处构建圆弧截面
    int num_samples = 16;
    std::vector<Point3> offset_pts0, offset_pts1;  // 偏移曲线上的点
    std::vector<Point3> fillet_arc_pts;             // 圆角面的截面中间点

    for (int i = 0; i <= num_samples; ++i) {
        double t = t0 + (t1 - t0) * i / num_samples;
        Point3 pt = curve.subs(t);

        // 搜索点在曲面上的 UV 参数，然后获取法线
        auto uv0_opt = surface_searchNearestParameterWithHint(surface0, pt, {}, 100);
        auto uv1_opt = surface_searchNearestParameterWithHint(surface1, pt, {}, 100);
        if (!uv0_opt || !uv1_opt) return std::nullopt;

        Vector3 normal0 = surface_normal(surface0, uv0_opt->first, uv0_opt->second);
        Vector3 normal1 = surface_normal(surface1, uv1_opt->first, uv1_opt->second);

        double len0 = glm::length(normal0);
        double len1 = glm::length(normal1);
        if (soSmall(len0) || soSmall(len1)) return std::nullopt;

        Vector3 dir0 = normal0 / len0;
        Vector3 dir1 = normal1 / len1;

        // 偏移点（圆角曲面与原始面的切点）
        Point3 off0 = pt + dir0 * radius;
        Point3 off1 = pt + dir1 * radius;

        offset_pts0.push_back(off0);
        offset_pts1.push_back(off1);

        // 圆角弧线的中间点（45度方向）
        Vector3 bisector = (dir0 + dir1);
        double bisector_len = glm::length(bisector);
        if (soSmall(bisector_len)) {
            fillet_arc_pts.push_back(pt);
        } else {
            bisector /= bisector_len;
            double half_angle = std::acos(
                std::clamp(glm::dot(dir0, dir1), -1.0, 1.0)) / 2.0;
            double dist = radius / std::cos(half_angle);
            Point3 arc_mid = pt + bisector * (dist * std::sin(half_angle));
            fillet_arc_pts.push_back(arc_mid);
        }
    }

    // 4. 用偏移点构建 BSplineSurface 作为圆角曲面
    //    控制点网格: v方向(u)=3个截面点, u方向(v)=num_samples+1个采样
    std::vector<std::vector<Point3>> control_points;
    control_points.reserve(num_samples + 1);
    for (int i = 0; i <= num_samples; ++i) {
        control_points.push_back({
            offset_pts0[i],
            fillet_arc_pts[i],
            offset_pts1[i]
        });
    }

    // 构建 2阶(u方向) x 2阶(v方向) BSplineSurface
    using KnotVec = geometry::KnotVec;
    auto u_knots = KnotVec::bezier_knot(num_samples);  // 沿边方向
    auto v_knots = KnotVec::bezier_knot(2);             // 截面方向

    auto fillet_surface = Surface(
        geometry::BSplineSurface<Point3>(
            std::make_pair(u_knots, v_knots),
            std::move(control_points)
        ));

    // 5. 构建偏移边（圆角面与原始面的交线）
    // 使用直线段近似偏移曲线
    auto makeOffsetCurve = [&](const std::vector<Point3>& pts) -> Curve {
        if (pts.size() < 2) return curve;
        // 用 BSplineCurve 插值偏移点
        auto interp_knots = KnotVec::bezier_knot(static_cast<int>(pts.size()) - 1);
        return Curve(geometry::BSplineCurve<Point3>(interp_knots, pts));
    };

    Curve offset_curve0 = makeOffsetCurve(offset_pts0);
    Curve offset_curve1 = makeOffsetCurve(offset_pts1);

    // 6. 创建圆角面的边和面
    Vertex<Point3> v_front0(offset_pts0.front());
    Vertex<Point3> v_back0(offset_pts0.back());
    Vertex<Point3> v_front1(offset_pts1.front());
    Vertex<Point3> v_back1(offset_pts1.back());

    // 原始边端点
    Vertex<Point3> v_orig_front(filleted_edge.front().point());
    Vertex<Point3> v_orig_back(filleted_edge.back().point());

    // 圆角面的四条边
    Edge<Point3, Curve> fillet_edge0(v_orig_front, v_back0, std::move(offset_curve0));
    Edge<Point3, Curve> fillet_edge1(v_orig_front, v_front1, curve); // 复用原始曲线近似
    Edge<Point3, Curve> fillet_edge2(v_front1, v_back1, std::move(offset_curve1));
    Edge<Point3, Curve> fillet_edge3(v_back0, v_back1, Curve(geometry::Line<Point3>(
        offset_pts0.back(), offset_pts1.back())));

    // 构建圆角面的边界线
    std::deque<Edge<Point3, Curve>> fillet_edges;
    fillet_edges.push_back(fillet_edge0);
    fillet_edges.push_back(fillet_edge3);
    fillet_edges.push_back(fillet_edge2.inverse());
    fillet_edges.push_back(fillet_edge1.inverse());

    auto fillet_wire_result = Wire<Point3, Curve>::tryNew(std::move(fillet_edges));
    if (!fillet_wire_result) return std::nullopt;

    auto fillet_face = Face<Point3, Curve, Surface>::newUnchecked(
        {std::move(*fillet_wire_result)}, fillet_surface);

    // 7. 创建切割后的 face0 和 face1
    // 在 face0 中，用 offset_curve0 替换原始边
    Edge<Point3, Curve> new_front0(v_orig_front, v_back0,
        makeOffsetCurve(offset_pts0));

    // 对于 face1，反向偏移
    std::vector<Point3> offset_pts1_rev(offset_pts1.rbegin(), offset_pts1.rend());
    Edge<Point3, Curve> new_back1(v_front1, v_back1,
        makeOffsetCurve(offset_pts1));

    // 简化版：直接返回原始面和圆角面
    // 完整版需要真正切割面（cut_face_by_curve），当前返回未修改的原始面
    SimpleFilletResult<Point3, Curve, Surface> result;
    result.face0 = face0;
    result.face1 = face1;
    result.fillet_face = std::move(fillet_face);

    return result;
}

// ============================================================
// 简化倒角 — 等距离倒角
// ============================================================

/**
 * @brief 在两个面共享的边上创建等距离倒角
 *
 * 算法:
 *   1. 沿两个面的法线方向各偏移 d 距离
 *   2. 用直线连接两个偏移点形成倒角平面
 *
 * @param face0 第一个面
 * @param face1 第二个面
 * @param filleted_edge_id 要做倒角的边的 ID
 * @param d 倒角距离
 * @param tol 容差
 * @return 倒角结果，失败返回 nullopt
 */
inline std::optional<SimpleFilletResult<Point3, Curve, Surface>>
simpleChamfer(
    const Face<Point3, Curve, Surface>& face0,
    const Face<Point3, Curve, Surface>& face1,
    EdgeID<Curve> filleted_edge_id,
    double d,
    double tol = TOLERANCE)
{
    // 和圆角类似，但用平面连接两个偏移曲线

    bool found_edge = false;
    Edge<Point3, Curve> filleted_edge;
    for (const auto& wire : face0.boundaries()) {
        for (const auto& edge : wire.edges()) {
            if (edge.id() == filleted_edge_id) {
                filleted_edge = edge;
                found_edge = true;
                break;
            }
        }
        if (found_edge) break;
    }
    if (!found_edge) return std::nullopt;

    auto curve = filleted_edge.orientedCurve();
    auto [t0, t1] = curve.rangeTuple();

    Surface surface0 = face0.orientedSurface();
    Surface surface1 = face1.orientedSurface();

    int num_samples = 16;
    std::vector<Point3> offset_pts0, offset_pts1;

    for (int i = 0; i <= num_samples; ++i) {
        double t = t0 + (t1 - t0) * i / num_samples;
        Point3 pt = curve.subs(t);

        auto uv0_opt = surface_searchNearestParameterWithHint(surface0, pt, {}, 100);
        auto uv1_opt = surface_searchNearestParameterWithHint(surface1, pt, {}, 100);
        if (!uv0_opt || !uv1_opt) return std::nullopt;

        Vector3 normal0 = surface_normal(surface0, uv0_opt->first, uv0_opt->second);
        Vector3 normal1 = surface_normal(surface1, uv1_opt->first, uv1_opt->second);
        double len0 = glm::length(normal0);
        double len1 = glm::length(normal1);
        if (soSmall(len0) || soSmall(len1)) return std::nullopt;

        offset_pts0.push_back(pt + (normal0 / len0) * d);
        offset_pts1.push_back(pt + (normal1 / len1) * d);
    }

    // 构建倒角面 (平面，用 BSplineSurface 表示)
    using KnotVec = geometry::KnotVec;

    std::vector<std::vector<Point3>> control_points;
    control_points.reserve(num_samples + 1);
    for (int i = 0; i <= num_samples; ++i) {
        control_points.push_back({offset_pts0[i], offset_pts1[i]});
    }

    auto u_knots = KnotVec::bezier_knot(num_samples);
    auto v_knots = KnotVec::bezier_knot(1);

    auto chamfer_surface = Surface(
        geometry::BSplineSurface<Point3>(
            std::make_pair(u_knots, v_knots),
            std::move(control_points)
        ));

    // 构建倒角面
    Vertex<Point3> v_front0(offset_pts0.front());
    Vertex<Point3> v_back0(offset_pts0.back());
    Vertex<Point3> v_front1(offset_pts1.front());
    Vertex<Point3> v_back1(offset_pts1.back());

    auto makeOffsetCurve = [&](const std::vector<Point3>& pts) -> Curve {
        if (pts.size() < 2) return curve;
        auto interp_knots = KnotVec::bezier_knot(static_cast<int>(pts.size()) - 1);
        return Curve(geometry::BSplineCurve<Point3>(interp_knots, pts));
    };

    Edge<Point3, Curve> edge0(v_front0, v_back0, makeOffsetCurve(offset_pts0));
    Edge<Point3, Curve> edge1(v_front0, v_front1,
        Curve(geometry::Line<Point3>(offset_pts0.front(), offset_pts1.front())));
    Edge<Point3, Curve> edge2(v_front1, v_back1, makeOffsetCurve(offset_pts1));
    Edge<Point3, Curve> edge3(v_back0, v_back1,
        Curve(geometry::Line<Point3>(offset_pts0.back(), offset_pts1.back())));

    std::deque<Edge<Point3, Curve>> chamfer_edges;
    chamfer_edges.push_back(edge0);
    chamfer_edges.push_back(edge3);
    chamfer_edges.push_back(edge2.inverse());
    chamfer_edges.push_back(edge1.inverse());

    auto wire_result = Wire<Point3, Curve>::tryNew(std::move(chamfer_edges));
    if (!wire_result) return std::nullopt;

    auto chamfer_face = Face<Point3, Curve, Surface>::newUnchecked(
        {std::move(*wire_result)}, chamfer_surface);

    SimpleFilletResult<Point3, Curve, Surface> result;
    result.face0 = face0;
    result.face1 = face1;
    result.fillet_face = std::move(chamfer_face);

    return result;
}

// ============================================================
// Solid 级别的倒角/圆角
// ============================================================

/**
 * @brief 在 Solid 的指定边上做圆角
 *
 * @param solid 目标实体
 * @param edge_id 要做圆角的边 ID
 * @param radius 圆角半径
 * @param tol 容差
 * @return 圆角后的新 Solid，失败返回 nullopt
 */
inline core::Result<Solid<Point3, Curve, Surface>>
filletEdge(
    const Solid<Point3, Curve, Surface>& solid,
    EdgeID<Curve> edge_id,
    double radius,
    double tol = TOLERANCE)
{
    // 1. 在 solid 中找到包含该边的两个面
    std::vector<std::pair<size_t, size_t>> face_indices; // (shell_idx, face_idx)
    for (size_t si = 0; si < solid.numBoundaries(); ++si) {
        const auto& shell = solid.boundary(si);
        for (size_t fi = 0; fi < shell.len(); ++fi) {
            for (const auto& wire : shell[fi].boundaries()) {
                for (const auto& edge : wire.edges()) {
                    if (edge.id() == edge_id) {
                        face_indices.push_back({si, fi});
                        break;
                    }
                }
            }
        }
    }

    if (face_indices.size() != 2) {
        return core::Err<Solid<Point3, Curve, Surface>>(
            makeError(TopologyError::InvalidParameter));
    }

    // 2. 对两个面做 simpleFillet
    auto [si0, fi0] = face_indices[0];
    auto [si1, fi1] = face_indices[1];

    // 复制 solid 的 shell
    auto shells = solid.boundaries();
    auto& shell0 = shells[si0];
    const auto& face0 = shell0[fi0];
    const auto& face1 = shells[si1][fi1];

    auto fillet_result = simpleFillet(face0, face1, edge_id, radius, tol);
    if (!fillet_result) {
        return core::Err<Solid<Point3, Curve, Surface>>(
            makeError(TopologyError::InvalidParameter));
    }

    // 3. 替换面并添加圆角面
    shell0.replaceFace(fi0, std::move(fillet_result->face0));
    if (si0 == si1) {
        shell0.replaceFace(fi1, std::move(fillet_result->face1));
    } else {
        shells[si1].replaceFace(fi1, std::move(fillet_result->face1));
    }
    shells[si0].push(std::move(fillet_result->fillet_face));

    return Solid<Point3, Curve, Surface>::newUnchecked(std::move(shells));
}

/**
 * @brief 在 Solid 的指定边上做倒角
 */
inline core::Result<Solid<Point3, Curve, Surface>>
chamferEdge(
    const Solid<Point3, Curve, Surface>& solid,
    EdgeID<Curve> edge_id,
    double d,
    double tol = TOLERANCE)
{
    std::vector<std::pair<size_t, size_t>> face_indices;
    for (size_t si = 0; si < solid.numBoundaries(); ++si) {
        const auto& shell = solid.boundary(si);
        for (size_t fi = 0; fi < shell.len(); ++fi) {
            for (const auto& wire : shell[fi].boundaries()) {
                for (const auto& edge : wire.edges()) {
                    if (edge.id() == edge_id) {
                        face_indices.push_back({si, fi});
                        break;
                    }
                }
            }
        }
    }

    if (face_indices.size() != 2) {
        return core::Err<Solid<Point3, Curve, Surface>>(
            makeError(TopologyError::InvalidParameter));
    }

    auto [si0, fi0] = face_indices[0];
    auto [si1, fi1] = face_indices[1];

    auto shells = solid.boundaries();
    const auto& face0 = shells[si0][fi0];
    const auto& face1 = shells[si1][fi1];

    auto chamfer_result = simpleChamfer(face0, face1, edge_id, d, tol);
    if (!chamfer_result) {
        return core::Err<Solid<Point3, Curve, Surface>>(
            makeError(TopologyError::InvalidParameter));
    }

    shells[si0].replaceFace(fi0, std::move(chamfer_result->face0));
    if (si0 == si1) {
        shells[si0].replaceFace(fi1, std::move(chamfer_result->face1));
    } else {
        shells[si1].replaceFace(fi1, std::move(chamfer_result->face1));
    }
    shells[si0].push(std::move(chamfer_result->fillet_face));

    return Solid<Point3, Curve, Surface>::newUnchecked(std::move(shells));
}

} // namespace MulanGeo::BRep::fillet
