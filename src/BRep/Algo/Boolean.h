/**
 * @file Boolean.h
 * @brief 布尔运算: union (or), intersection (and), difference (cut)
 *
 * 基于 truck-shapeops::transversal 的 C++ 移植。
 *
 * 完整算法流程:
 *   1. 将两个 Shell 细化为三角网格
 *   2. 用三角形-三角形碰撞检测找到相交线段
 *   3. 将相交线段组装成交线 (Polyline) → LoopsStore
 *   4. 用交线分割面 (divide_face)
 *   5. 将分割后的面分类为 And/Or/Unknown
 *   6. 用连通分量 + 射线穿面法判定 Unknown 面归属
 *   7. 按操作类型选取面，合并为最终 Shell
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
#include "BVH.h"
#include "PolylineAssembly.h"
#include "DivideFace.h"

#include <mulan/Geometry/Types.h>
#include <mulan/Geometry/Tolerance.h>
#include <mulan/Geometry/Specified/Line.h>
#include <mulan/Geometry/Nurbs/BSplineCurve.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>
#include <utility>

namespace mulan::brep::boolean {

using geometry::Point3;
using geometry::Vector3;
using geometry::Vector2;
using geometry::near;
using geometry::near2;
using geometry::soSmall;

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
// 面分类 — 判断面在另一个 Shell 的内部或外部（射线穿面法）
// ============================================================

inline ShapeOpStatus classifyFace(
    const Face<Point3, Curve, Surface>& face,
    const tessellation::TriMesh& other_mesh)
{
    if (face.boundaries().empty() || face.boundary(0).len() == 0) {
        return ShapeOpStatus::Unknown;
    }

    Point3 test_point = face.boundary(0)[0].front().point();
    bool inside = tessellation::pointInSolid(other_mesh, test_point);
    return inside ? ShapeOpStatus::And : ShapeOpStatus::Or;
}

// ============================================================
// 布尔核心算法 — 完整管线
// ============================================================

inline core::Result<Solid<Point3, Curve, Surface>> booleanOp(
    const Solid<Point3, Curve, Surface>& solid0,
    const Solid<Point3, Curve, Surface>& solid1,
    BoolOp op,
    double tol = geometry::TOLERANCE)
{
    Shell<Point3, Curve, Surface> result_shell;

    // 三角化两个 Solid
    auto mesh0_all = tessellation::triangulateSolidMerged(solid0, tol);
    auto mesh1_all = tessellation::triangulateSolidMerged(solid1, tol);

    // Step 1: 碰撞检测
    auto segments = tessellation::extractInterferenceAccelerated(mesh0_all, mesh1_all);
    bool has_intersection = !segments.empty();

    if (!has_intersection) {
        // 无相交 → 两个体完全分离，直接按操作类型选取
        switch (op) {
        case BoolOp::Union:
            for (size_t i = 0; i < solid0.numBoundaries(); ++i)
                for (size_t fi = 0; fi < solid0.boundary(i).len(); ++fi)
                    result_shell.push(solid0.boundary(i)[fi]);
            for (size_t j = 0; j < solid1.numBoundaries(); ++j)
                for (size_t fi = 0; fi < solid1.boundary(j).len(); ++fi)
                    result_shell.push(solid1.boundary(j)[fi]);
            break;
        case BoolOp::Intersection:
            // 不相交 → 交集为空
            return core::Err<Solid<Point3, Curve, Surface>>(
                makeError(TopologyError::EmptyShell));
        case BoolOp::Difference:
            for (size_t i = 0; i < solid0.numBoundaries(); ++i)
                for (size_t fi = 0; fi < solid0.boundary(i).len(); ++fi)
                    result_shell.push(solid0.boundary(i)[fi]);
            break;
        }
    } else {
        // Step 2: 对两个 shell 分别构建 LoopsStore 并分割面

        // --- 处理 solid0 的面 ---
        for (size_t i = 0; i < solid0.numBoundaries(); ++i) {
            const auto& shell0 = solid0.boundary(i);
            auto loops_store0 = createLoopsStore(shell0, segments, tol);

            auto cls0 = divideFaces(shell0, loops_store0, tol);
            if (!cls0) {
                // divide_face 失败，回退到简单分类
                for (size_t fi = 0; fi < shell0.len(); ++fi) {
                    auto status = classifyFace(shell0[fi], mesh1_all);
                    if ((op == BoolOp::Union && status == ShapeOpStatus::Or) ||
                        (op == BoolOp::Intersection && status == ShapeOpStatus::And) ||
                        (op == BoolOp::Difference && status == ShapeOpStatus::And))
                        result_shell.push(shell0[fi]);
                }
                continue;
            }

            cls0->integrateByComponent();
            auto [and0, or0, unknown0] = cls0->andOrUnknown();

            // 射线穿面分类 Unknown 面
            for (size_t fi = 0; fi < unknown0.len(); ++fi) {
                auto status = classifyFace(unknown0[fi], mesh1_all);
                if (status == ShapeOpStatus::And) and0.push(unknown0[fi]);
                else or0.push(unknown0[fi]);
            }

            // 按操作类型选取
            switch (op) {
            case BoolOp::Union:
                for (size_t fi = 0; fi < or0.len(); ++fi)
                    result_shell.push(or0[fi]);
                break;
            case BoolOp::Intersection:
                for (size_t fi = 0; fi < and0.len(); ++fi)
                    result_shell.push(and0[fi]);
                break;
            case BoolOp::Difference:
                for (size_t fi = 0; fi < and0.len(); ++fi)
                    result_shell.push(and0[fi]);
                break;
            }
        }

        // --- 处理 solid1 的面 ---
        for (size_t j = 0; j < solid1.numBoundaries(); ++j) {
            const auto& shell1 = solid1.boundary(j);
            auto loops_store1 = createLoopsStore(shell1, segments, tol);

            auto cls1 = divideFaces(shell1, loops_store1, tol);
            if (!cls1) {
                for (size_t fi = 0; fi < shell1.len(); ++fi) {
                    auto status = classifyFace(shell1[fi], mesh0_all);
                    if ((op == BoolOp::Union && status == ShapeOpStatus::Or) ||
                        (op == BoolOp::Intersection && status == ShapeOpStatus::And) ||
                        (op == BoolOp::Difference && status == ShapeOpStatus::Or))
                        result_shell.push(op == BoolOp::Difference
                            ? shell1[fi].inverse() : shell1[fi]);
                }
                continue;
            }

            cls1->integrateByComponent();
            auto [and1, or1, unknown1] = cls1->andOrUnknown();

            for (size_t fi = 0; fi < unknown1.len(); ++fi) {
                auto status = classifyFace(unknown1[fi], mesh0_all);
                if (status == ShapeOpStatus::And) and1.push(unknown1[fi]);
                else or1.push(unknown1[fi]);
            }

            switch (op) {
            case BoolOp::Union:
                for (size_t fi = 0; fi < or1.len(); ++fi)
                    result_shell.push(or1[fi]);
                break;
            case BoolOp::Intersection:
                for (size_t fi = 0; fi < and1.len(); ++fi)
                    result_shell.push(and1[fi]);
                break;
            case BoolOp::Difference:
                for (size_t fi = 0; fi < or1.len(); ++fi)
                    result_shell.push(or1[fi].inverse());
                break;
            }
        }
    }

    if (result_shell.isEmpty()) {
        return core::Err<Solid<Point3, Curve, Surface>>(
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

inline core::Result<Solid<Point3, Curve, Surface>> unionOp(
    const Solid<Point3, Curve, Surface>& a,
    const Solid<Point3, Curve, Surface>& b,
    double tol = geometry::TOLERANCE)
{
    return booleanOp(a, b, BoolOp::Union, tol);
}

inline core::Result<Solid<Point3, Curve, Surface>> intersection(
    const Solid<Point3, Curve, Surface>& a,
    const Solid<Point3, Curve, Surface>& b,
    double tol = geometry::TOLERANCE)
{
    return booleanOp(a, b, BoolOp::Intersection, tol);
}

inline core::Result<Solid<Point3, Curve, Surface>> difference(
    const Solid<Point3, Curve, Surface>& a,
    const Solid<Point3, Curve, Surface>& b,
    double tol = geometry::TOLERANCE)
{
    return booleanOp(a, b, BoolOp::Difference, tol);
}

} // namespace mulan::BRep::boolean