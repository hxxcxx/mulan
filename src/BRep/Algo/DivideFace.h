/**
 * @file DivideFace.h
 * @brief 布尔运算核心: 面分割 (divide_face)
 *
 * 将面沿相交线分割为子面，并分类为 And/Or/Unknown。
 *
 * 基于 truck-shapeops::transversal::divide_face 的 C++ 移植。
 *
 * 算法流程:
 *   1. 将相交线 (BoundaryWire) 投影到面的 (u,v) 参数空间
 *   2. 在参数空间中形成多边形区域
 *   3. 用 2D 点包含测试将区域分组为独立子面
 *   4. 每个子面继承其边界线的状态 (And/Or/Unknown)
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
#include "../Topology/ID.h"
#include "../CurveSurface/CurveSurface.h"
#include "../CurveSurface/CurveOps.h"
#include "Collision.h"
#include "Triangulation.h"
#include "PolylineAssembly.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>
#include <MulanGeo/Geometry/Algo/surface/search.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace MulanGeo::BRep::boolean {

using geometry::Point3;
using geometry::Vector3;
using geometry::Vector2;
using geometry::near;
using geometry::soSmall;
using geometry::TOLERANCE;

// ============================================================
// 布尔运算面状态
// ============================================================

/// 面相对于两个壳的分类状态
enum class ShapeOpStatus {
    And,    // 面在两个壳的交集内部
    Or,     // 面在两个壳的并集内部
    Unknown, // 无法直接判定，需后续分类
};

/// 带状态的面
template<typename P, typename C, typename S>
struct ClassifiedFace {
    Face<P, C, S> face;
    ShapeOpStatus status;
};

// ============================================================
// 边界线 — 带状态标记的 Wire
// ============================================================

template<typename P, typename C>
struct BoundaryWire {
    Wire<P, C> wire;
    ShapeOpStatus status;

    static BoundaryWire newAnd(Wire<P, C> w) {
        return {std::move(w), ShapeOpStatus::And};
    }
    static BoundaryWire newOr(Wire<P, C> w) {
        return {std::move(w), ShapeOpStatus::Or};
    }
    static BoundaryWire newUnknown(Wire<P, C> w) {
        return {std::move(w), ShapeOpStatus::Unknown};
    }
};

/// 一个面的所有相交环
template<typename P, typename C>
using Loops = std::vector<BoundaryWire<P, C>>;

/// 所有面的相交环索引
template<typename P, typename C>
using LoopsStore = std::vector<Loops<P, C>>;

// ============================================================
// 参数空间折线 — 将 3D 折线投影到曲面 (u,v) 空间
// ============================================================

struct Polyline2D {
    std::vector<Vector2> points;

    /// 计算有符号面积（用于判断方向）
    double signedArea() const {
        if (points.size() < 3) return 0.0;
        double area = 0.0;
        for (size_t i = 0; i < points.size(); ++i) {
            size_t j = (i + 1) % points.size();
            area += static_cast<double>(points[i].x) * static_cast<double>(points[j].y);
            area -= static_cast<double>(points[j].x) * static_cast<double>(points[i].y);
        }
        return area / 2.0;
    }

    bool isPositive() const { return signedArea() > 0.0; }

    /// 2D 点包含测试（射线法）
    bool include(const Vector2& pt) const {
        if (points.size() < 3) return false;
        int crossings = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            size_t j = (i + 1) % points.size();
            double y0 = static_cast<double>(points[i].y);
            double y1 = static_cast<double>(points[j].y);
            double x0 = static_cast<double>(points[i].x);
            double x1 = static_cast<double>(points[j].x);
            double py = static_cast<double>(pt.y);
            double px = static_cast<double>(pt.x);

            if ((y0 <= py && y1 > py) || (y1 <= py && y0 > py)) {
                double t = (py - y0) / (y1 - y0);
                double x_intersect = x0 + t * (x1 - x0);
                if (px < x_intersect) {
                    crossings++;
                }
            }
        }
        return (crossings % 2) == 1;
    }
};

// ============================================================
// 将 Wire 投影到面的参数空间
// ============================================================

/// 将 Wire 的 3D 点离散化并投影到曲面参数空间
inline std::optional<Polyline2D> projectWireToParameterSpace(
    const Face<Point3, Curve, Surface>& face,
    const Wire<Point3, Curve>& wire,
    int num_samples = 32)
{
    auto surface = face.surface();
    Polyline2D result;
    result.points.reserve(wire.len() * num_samples);

    // 获取参数范围用于 presearch
    auto param_range = surface.parameterRange();
    std::pair<std::pair<double, double>, std::pair<double, double>> range = {
        {static_cast<double>(param_range.first.first.value),
         static_cast<double>(param_range.first.second.value)},
        {static_cast<double>(param_range.second.first.value),
         static_cast<double>(param_range.second.second.value)}
    };

    for (size_t ei = 0; ei < wire.len(); ++ei) {
        const auto& edge = wire[ei];
        auto curve = edge.curve();
        auto [t0, t1] = curve.rangeTuple();

        int start = (ei == 0) ? 0 : 1; // 避免重复点
        for (int i = start; i <= num_samples; ++i) {
            double t = t0 + (t1 - t0) * i / num_samples;
            Point3 pt = curve.subs(t);

            // 先用暴力搜索获取初始提示
            auto hint = geometry::Algo::Surface::presearch(surface, pt, range, 20);
            // 再用 Newton 法精确求解
            auto uv = geometry::Algo::Surface::searchParameter(
                surface, pt, hint, 50);
            if (!uv) return std::nullopt;
            result.points.push_back(Vector2(uv->first, uv->second));
        }
    }

    return result;
}

// ============================================================
// WireChunk — 参数空间中的一个区域块
// ============================================================

struct WireChunk {
    Polyline2D poly;
    size_t wire_index;
    ShapeOpStatus status;
};

// ============================================================
// divide_one_face — 将单个面沿相交环分割为子面
// ============================================================

/// 将面分割为子面，每个子面带分类状态
inline std::optional<std::vector<ClassifiedFace<Point3, Curve, Surface>>>
divideOneFace(
    const Face<Point3, Curve, Surface>& face,
    const Loops<Point3, Curve>& loops,
    double tol = TOLERANCE)
{
    if (loops.empty()) return std::nullopt;

    // 1. 将每个相交环投影到参数空间
    std::vector<std::pair<Polyline2D, size_t>> positive_loops;  // 外环
    std::vector<std::pair<Polyline2D, size_t>> negative_loops;  // 内环（孔洞）

    for (size_t i = 0; i < loops.size(); ++i) {
        auto poly = projectWireToParameterSpace(face, loops[i].wire);
        if (!poly) return std::nullopt;

        if (poly->isPositive()) {
            positive_loops.emplace_back(std::move(*poly), i);
        } else {
            negative_loops.emplace_back(std::move(*poly), i);
        }
    }

    // 如果没有正向环，说明所有相交线形成孔洞
    if (positive_loops.empty()) {
        // 整个面状态为 Unknown
        std::vector<ClassifiedFace<Point3, Curve, Surface>> result;
        result.push_back({face, ShapeOpStatus::Unknown});
        return result;
    }

    // 2. 将负向环（孔洞）分配给包含它们的正向环
    std::vector<std::vector<size_t>> holes_for_positive(positive_loops.size());
    for (size_t hi = 0; hi < negative_loops.size(); ++hi) {
        const auto& hole_poly = negative_loops[hi].first;
        // 找到包含这个孔洞的最小正向环
        size_t best_parent = 0;
        double best_area = std::numeric_limits<double>::max();
        for (size_t pi = 0; pi < positive_loops.size(); ++pi) {
            if (positive_loops[pi].first.include(hole_poly.points[0])) {
                double area = positive_loops[pi].first.signedArea();
                if (area < best_area) {
                    best_area = area;
                    best_parent = pi;
                }
            }
        }
        holes_for_positive[best_parent].push_back(hi);
    }

    // 3. 构建子面
    auto surface = face.surface();
    std::vector<ClassifiedFace<Point3, Curve, Surface>> result;
    result.reserve(positive_loops.size());

    for (size_t pi = 0; pi < positive_loops.size(); ++pi) {
        size_t loop_idx = positive_loops[pi].second;

        // 收集这个子面的所有边界线
        std::vector<Wire<Point3, Curve>> sub_boundaries;
        sub_boundaries.push_back(loops[loop_idx].wire);

        // 添加孔洞
        for (size_t hi : holes_for_positive[pi]) {
            size_t hole_loop_idx = negative_loops[hi].second;
            sub_boundaries.push_back(loops[hole_loop_idx].wire);
        }

        // 确定状态：优先取非 Unknown 状态
        ShapeOpStatus status = loops[loop_idx].status;
        if (status == ShapeOpStatus::Unknown) {
            for (size_t hi : holes_for_positive[pi]) {
                size_t hole_loop_idx = negative_loops[hi].second;
                if (loops[hole_loop_idx].status != ShapeOpStatus::Unknown) {
                    status = loops[hole_loop_idx].status;
                    break;
                }
            }
        }

        auto sub_face = Face<Point3, Curve, Surface>::newUnchecked(
            std::move(sub_boundaries), surface);
        if (!face.orientation()) sub_face.invert();

        result.push_back({std::move(sub_face), status});
    }

    return result;
}

// ============================================================
// 面分类结果集
// ============================================================

template<typename P, typename C, typename S>
class FacesClassification {
public:
    void push(Face<P, C, S> face, ShapeOpStatus status) {
        status_map_[face.id()] = status;
        faces_.push_back(std::move(face));
    }

    const std::vector<Face<P, C, S>>& faces() const { return faces_; }

    ShapeOpStatus status(size_t i) const {
        auto it = status_map_.find(faces_[i].id());
        return (it != status_map_.end()) ? it->second : ShapeOpStatus::Unknown;
    }

    /// 按状态分类返回三个 Shell
    std::array<Shell<P, C, S>, 3> andOrUnknown() const {
        Shell<P, C, S> and_shell, or_shell, unknown_shell;
        for (const auto& f : faces_) {
            auto it = status_map_.find(f.id());
            auto s = (it != status_map_.end()) ? it->second : ShapeOpStatus::Unknown;
            switch (s) {
            case ShapeOpStatus::And: and_shell.push(f); break;
            case ShapeOpStatus::Or: or_shell.push(f); break;
            case ShapeOpStatus::Unknown: unknown_shell.push(f); break;
            }
        }
        return {and_shell, or_shell, unknown_shell};
    }

    /// 通过连通分量分类 Unknown 面
    void integrateByComponent() {
        // 收集 And/Or 的边界 EdgeID
        std::unordered_set<EdgeID<C>, typename EdgeID<C>::Hash> and_edges;
        std::unordered_set<EdgeID<C>, typename EdgeID<C>::Hash> or_edges;

        for (const auto& f : faces_) {
            auto it = status_map_.find(f.id());
            if (it == status_map_.end()) continue;
            for (const auto& wire : f.boundaries()) {
                for (const auto& edge : wire.edges()) {
                    if (it->second == ShapeOpStatus::And) {
                        and_edges.insert(edge.id());
                    } else if (it->second == ShapeOpStatus::Or) {
                        or_edges.insert(edge.id());
                    }
                }
            }
        }

        // 对 Unknown 面，检查其边界是否与 And 或 Or 共享边
        for (auto& [face_id, status] : status_map_) {
            if (status != ShapeOpStatus::Unknown) continue;

            // 找到这个面
            for (const auto& f : faces_) {
                if (f.id() != face_id) continue;
                bool touches_and = false, touches_or = false;
                for (const auto& wire : f.boundaries()) {
                    for (const auto& edge : wire.edges()) {
                        if (and_edges.count(edge.id())) touches_and = true;
                        if (or_edges.count(edge.id())) touches_or = true;
                    }
                }
                if (touches_and) status = ShapeOpStatus::And;
                else if (touches_or) status = ShapeOpStatus::Or;
                break;
            }
        }
    }

private:
    std::vector<Face<P, C, S>> faces_;
    std::unordered_map<FaceID<S>, ShapeOpStatus, typename FaceID<S>::Hash> status_map_;
};

// ============================================================
// divide_faces — 对整个 Shell 的所有面执行分割
// ============================================================

/// 分割 Shell 中的所有面，返回分类结果
inline std::optional<FacesClassification<Point3, Curve, Surface>>
divideFaces(
    const Shell<Point3, Curve, Surface>& shell,
    const LoopsStore<Point3, Curve>& loops_store,
    double tol = TOLERANCE)
{
    FacesClassification<Point3, Curve, Surface> result;

    for (size_t fi = 0; fi < shell.len(); ++fi) {
        const auto& face = shell[fi];
        const auto& loops = loops_store[fi];

        if (loops.empty()) {
            // 没有相交线，面状态为 Unknown
            result.push(face, ShapeOpStatus::Unknown);
            continue;
        }

        // 检查是否所有环都是 Unknown
        bool all_unknown = true;
        for (const auto& loop : loops) {
            if (loop.status != ShapeOpStatus::Unknown) {
                all_unknown = false;
                break;
            }
        }

        if (all_unknown) {
            result.push(face, ShapeOpStatus::Unknown);
            continue;
        }

        // 分割面
        auto sub_faces = divideOneFace(face, loops, tol);
        if (!sub_faces) return std::nullopt;

        for (auto& [sub_face, status] : *sub_faces) {
            result.push(std::move(sub_face), status);
        }
    }

    return result;
}

// ============================================================
// 创建 LoopsStore — 从干涉线段构建相交环
// ============================================================

/// 将干涉线段组装成折线，然后分配给对应的面
inline LoopsStore<Point3, Curve> createLoopsStore(
    const Shell<Point3, Curve, Surface>& shell,
    const std::vector<tessellation::LineSegment>& segments,
    double tol = TOLERANCE * 10.0)
{
    LoopsStore<Point3, Curve> store(shell.len());

    if (segments.empty()) return store;

    // 组装折线
    auto polylines = assemblePolylines(segments, tol);

    // 对每条折线，判断它在哪个面上
    for (auto& polyline : polylines) {
        if (polyline.points.size() < 2) continue;

        // 找到包含这条折线最多点的面
        size_t best_face = 0;
        int best_count = 0;
        for (size_t fi = 0; fi < shell.len(); ++fi) {
            auto surface = shell[fi].surface();
            auto param_range = surface.parameterRange();
            std::pair<std::pair<double, double>, std::pair<double, double>> range = {
                {static_cast<double>(param_range.first.first.value),
                 static_cast<double>(param_range.first.second.value)},
                {static_cast<double>(param_range.second.first.value),
                 static_cast<double>(param_range.second.second.value)}
            };

            int count = 0;
            for (const auto& pt : polyline.points) {
                // 检查点是否在曲面上（参数空间搜索）
                auto hint = geometry::Algo::Surface::presearch(surface, pt, range, 10);
                auto uv = geometry::Algo::Surface::searchParameter(
                    surface, pt, hint, 20);
                if (uv) count++;
            }
            if (count > best_count) {
                best_count = count;
                best_face = fi;
            }
        }

        // 用折线创建一个 Wire
        std::deque<Edge<Point3, Curve>> edges;
        for (size_t i = 0; i + 1 < polyline.points.size(); ++i) {
            auto curve = Curve(geometry::Line<Point3>(
                polyline.points[i], polyline.points[i + 1]));
            Vertex<Point3> v0(polyline.points[i]);
            Vertex<Point3> v1(polyline.points[i + 1]);
            auto edge = Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(curve));
            edges.push_back(std::move(edge));
        }

        auto wire_result = Wire<Point3, Curve>::tryNew(edges);
        if (wire_result) {
            // 判断是 And 还是 Or（简化：默认 Unknown）
            store[best_face].push_back(
                BoundaryWire<Point3, Curve>::newUnknown(std::move(*wire_result)));
        }
    }

    return store;
}

} // namespace MulanGeo::BRep::boolean
