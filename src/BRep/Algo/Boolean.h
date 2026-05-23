/**
 * @file Boolean.h
 * @brief 布尔运算: union (or), intersection (and), difference (cut)
 *
 * 基于 truck-shapeops::transversal 的 C++ 移植。
 *
 * 当前实现为简化版本 — 仅做面分类（射线穿面法判定内外），
 * 不做面分割。这意味着:
 *   - 不相交的实体: 结果正确
 *   - 部分相交: 结果近似（相交面不被分割，整体归类为 And/Or）
 *   - 完整布尔运算需要 LoopsStore + divide_face 面分割拓扑，
 *     将在后续版本补充
 *
 * 完整算法流程 (truck-shapeops):
 *   1. 将两个 Shell 细化为三角网格
 *   2. 用三角形-三角形碰撞检测找到相交线段
 *   3. 将相交线段组装成交线 (Polyline)
 *   4. 用交线分割面 (divide_face) — TODO
 *   5. 将分割后的面分类为 And/Or/Unknown
 *   6. 用射线穿面法判定 Unknown 面归属
 *   7. 合并结果面为最终 Shell
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
#include "Triangulation.h"
#include "Collision.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>
#include <utility>

namespace MulanGeo::BRep::boolean {

using Geometry::Point3;
using Geometry::Vector3;
using Geometry::Vector2;
using Geometry::near;
using Geometry::near2;
using Geometry::soSmall;

// ============================================================
// 布尔运算状态标记
// ============================================================

enum class BoolOp {
    Union,
    Intersection,
    Difference,
};

enum class FaceClass {
    Unknown,
    And,
    Or,
};

// ============================================================
// 折线组装 — 从线段集合构建有序折线
// ============================================================

inline std::vector<tessellation::PolylineCurve> assemblePolylines(
    const std::vector<tessellation::LineSegment>& segments,
    double tol = Geometry::TOLERANCE * 10.0)
{
    if (segments.empty()) return {};

    std::vector<bool> used(segments.size(), false);
    std::vector<tessellation::PolylineCurve> polylines;

    for (size_t start = 0; start < segments.size(); ++start) {
        if (used[start]) continue;
        used[start] = true;

        tessellation::PolylineCurve plc;
        plc.points.push_back(segments[start].p[0]);
        plc.points.push_back(segments[start].p[1]);
        plc.params.push_back(0.0);
        plc.params.push_back(1.0);

        bool extended = true;
        while (extended) {
            extended = false;
            Point3 head = plc.points.front();
            Point3 tail = plc.points.back();

            for (size_t j = 0; j < segments.size(); ++j) {
                if (used[j]) continue;

                if (near(tail, segments[j].p[0], tol)) {
                    plc.points.push_back(segments[j].p[1]);
                    plc.params.push_back(static_cast<double>(plc.params.size()));
                    tail = segments[j].p[1];
                    used[j] = true;
                    extended = true;
                } else if (near(tail, segments[j].p[1], tol)) {
                    plc.points.push_back(segments[j].p[0]);
                    plc.params.push_back(static_cast<double>(plc.params.size()));
                    tail = segments[j].p[0];
                    used[j] = true;
                    extended = true;
                } else if (near(head, segments[j].p[0], tol)) {
                    plc.points.insert(plc.points.begin(), segments[j].p[1]);
                    plc.params.insert(plc.params.begin(), 0.0);
                    head = segments[j].p[1];
                    used[j] = true;
                    extended = true;
                } else if (near(head, segments[j].p[1], tol)) {
                    plc.points.insert(plc.points.begin(), segments[j].p[0]);
                    plc.params.insert(plc.params.begin(), 0.0);
                    head = segments[j].p[0];
                    used[j] = true;
                    extended = true;
                }
            }
        }

        polylines.push_back(std::move(plc));
    }

    return polylines;
}

// ============================================================
// 面分类 — 判断面在另一个 Shell 的内部或外部
// ============================================================

inline FaceClass classifyFaceAgainstShell(
    const Face<Point3, Curve, Surface>& face,
    const tessellation::TriMesh& other_mesh,
    BoolOp op)
{
    if (face.boundaries().empty() || face.boundary(0).len() == 0) {
        return FaceClass::Unknown;
    }

    Point3 test_point = face.boundary(0)[0].front().point();
    bool inside = tessellation::pointInSolid(other_mesh, test_point);

    if (op == BoolOp::Intersection) {
        return inside ? FaceClass::And : FaceClass::Or;
    } else {
        return inside ? FaceClass::Or : FaceClass::And;
    }
}

// ============================================================
// 布尔核心算法
// ============================================================

inline Core::Result<Solid<Point3, Curve, Surface>> booleanOp(
    const Solid<Point3, Curve, Surface>& solid0,
    const Solid<Point3, Curve, Surface>& solid1,
    BoolOp op,
    double tol = Geometry::TOLERANCE)
{
    Shell<Point3, Curve, Surface> result_shell;

    for (size_t si = 0; si < solid0.numBoundaries(); ++si) {
        const auto& shell0 = solid0.boundary(si);
        auto mesh0 = tessellation::triangulateShell(shell0, tol);

        for (size_t sj = 0; sj < solid1.numBoundaries(); ++sj) {
            const auto& shell1 = solid1.boundary(sj);
            auto mesh1 = tessellation::triangulateShell(shell1, tol);

            auto segments = tessellation::extractInterference(mesh0, mesh1);

            if (segments.empty()) {
                continue;
            }

            // TODO: 用交线分割面 (divide_face)，实现完整布尔运算
            // 当前简化版：不做面分割，仅通过射线穿面法分类
            (void)op;
        }
    }

    for (size_t i = 0; i < solid0.numBoundaries(); ++i) {
        const auto& shell0 = solid0.boundary(i);
        auto mesh0 = tessellation::triangulateShell(shell0, tol);
        auto mesh1_all = tessellation::triangulateSolidMerged(solid1, tol);

        for (size_t fi = 0; fi < shell0.len(); ++fi) {
            auto cls = classifyFaceAgainstShell(shell0[fi], mesh1_all, op);
            if (op == BoolOp::Union && cls == FaceClass::Or) {
                result_shell.push(shell0[fi]);
            } else if (op == BoolOp::Intersection && cls == FaceClass::And) {
                result_shell.push(shell0[fi]);
            } else if (op == BoolOp::Difference && cls == FaceClass::And) {
                result_shell.push(shell0[fi]);
            }
        }
    }

    for (size_t j = 0; j < solid1.numBoundaries(); ++j) {
        const auto& shell1 = solid1.boundary(j);
        auto mesh1 = tessellation::triangulateShell(shell1, tol);
        auto mesh0_all = tessellation::triangulateSolidMerged(solid0, tol);

        for (size_t fi = 0; fi < shell1.len(); ++fi) {
            auto cls = classifyFaceAgainstShell(shell1[fi], mesh0_all, op);
            if (op == BoolOp::Union && cls == FaceClass::Or) {
                result_shell.push(shell1[fi]);
            } else if (op == BoolOp::Intersection && cls == FaceClass::And) {
                result_shell.push(shell1[fi]);
            } else if (op == BoolOp::Difference && cls == FaceClass::Or) {
                auto inv_face = shell1[fi].inverse();
                result_shell.push(inv_face);
            }
        }
    }

    if (result_shell.isEmpty()) {
        return Core::Err<Solid<Point3, Curve, Surface>>(
            makeError(TopologyError::EmptyShell));
    }

    auto components = result_shell.connectedComponents();
    if (components.size() != 1) {
        std::vector<Shell<Point3, Curve, Surface>> shells;
        for (auto& comp : components) {
            shells.push_back(std::move(comp));
        }
        return Solid<Point3, Curve, Surface>::newUnchecked(std::move(shells));
    }

    return Solid<Point3, Curve, Surface>::newUnchecked({std::move(result_shell)});
}

inline Core::Result<Solid<Point3, Curve, Surface>> unionOp(
    const Solid<Point3, Curve, Surface>& a,
    const Solid<Point3, Curve, Surface>& b,
    double tol = Geometry::TOLERANCE)
{
    return booleanOp(a, b, BoolOp::Union, tol);
}

inline Core::Result<Solid<Point3, Curve, Surface>> intersection(
    const Solid<Point3, Curve, Surface>& a,
    const Solid<Point3, Curve, Surface>& b,
    double tol = Geometry::TOLERANCE)
{
    return booleanOp(a, b, BoolOp::Intersection, tol);
}

inline Core::Result<Solid<Point3, Curve, Surface>> difference(
    const Solid<Point3, Curve, Surface>& a,
    const Solid<Point3, Curve, Surface>& b,
    double tol = Geometry::TOLERANCE)
{
    return booleanOp(a, b, BoolOp::Difference, tol);
}

} // namespace MulanGeo::BRep::boolean