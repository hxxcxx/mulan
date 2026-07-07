/**
 * @file segment_intersect.h
 * @brief 2D 线段求交 — 成对求交与全量求交
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::LineSegmentIntersection，适配至 mulan::math。
 *
 * 提供：
 *  - segmentsIntersect(a,b)：两条线段是否相交，可取交点与参数。
 *  - findAllSegmentIntersections(segments)：一组线段间的所有交点（含相交线段下标）。
 *
 * 复杂度说明（重要，勿误导）：
 *  - 成对求交 O(1)。
 *  - 全量求交为暴力 O(n²)，对中等规模点集足够。
 *  - 原 BeyondConvex 的 Bentley-Ottmann 扫描线版本未移植：其实现每处理一个事件就
 *    清空并重建整棵状态树（实际退化为 O(n² log n)，并非声称的 O((n+k)log n)），
 *    且状态序比较器缺少线段下标作 tie-breaker，对共享端点/同斜率的线段会产生
 *    漏插与查找失败。如需大规模高性能版本，应另起一个严格正确的事件调度实现。
 *
 * 容差：参数域判定走 Tolerance.paramEps（t∈[0,1] 的端点比较）。
 */
#pragma once

#include "../linalg/vec2.h"
#include "../geom/point.h"
#include "../geom/line.h"  // Segment2
#include "../scalar/tolerance.h"

#include <vector>

namespace mulan::math {

/// 一组线段求交的命中记录：交点 + 两条相交线段的下标。
struct SegmentIntersection {
    Point2 point;       ///< 交点
    int segmentA = -1;  ///< 第一条线段下标
    int segmentB = -1;  ///< 第二条线段下标
};

/// 两条线段是否相交。
///
/// 基于参数化求解：segA.start + dA * sa = segB.start + dB * sb，
/// 其中 dA = segA.end - segA.start，dB 同理。
/// 解得 sa,sb 后，二者均 ∈ [0,1]（含端点，容差 paramEps）即相交。
///
/// 输出参数（可选）：outSa/out_sb 为对应参数；outPoint 为交点。
/// 平行/共线视为不相交（返回 false）——共线重叠需另行处理。
inline bool segmentsIntersect(const Segment2& segA, const Segment2& segB, double* outSa = nullptr,
                              double* outSb = nullptr, Point2* outPoint = nullptr,
                              const Tolerance& tol = defaultTolerance()) {
    Vec2 dA = segA.direction();
    Vec2 dB = segB.direction();
    double denom = dA.cross(dB);
    if (std::abs(denom) < 1e-15) {
        return false;  // 平行/共线
    }
    Vec2 ab = Vec2(segB.start.x - segA.start.x, segB.start.y - segA.start.y);
    double sa = ab.cross(dB) / denom;
    double sb = ab.cross(dA) / denom;
    if (sa < -tol.paramEps || sa > 1.0 + tol.paramEps || sb < -tol.paramEps || sb > 1.0 + tol.paramEps) {
        return false;
    }
    if (outSa)
        *outSa = sa;
    if (outSb)
        *outSb = sb;
    if (outPoint)
        *outPoint = segA.pointAt(sa);
    return true;
}

/// 一组线段间的所有两两交点（暴力 O(n²)）。
///
/// 返回的每条记录中 segmentA < segmentB（按输入下标）。仅返回真交点（不含共享端点，
/// 因共享端点处两线段参数在容差内恰好为 0/1，会被判定为相交并包含——这是预期行为，
/// 共享端点视为交点）。
inline std::vector<SegmentIntersection> findAllSegmentIntersections(const std::vector<Segment2>& segments,
                                                                    const Tolerance& tol = defaultTolerance()) {
    std::vector<SegmentIntersection> result;
    const size_t n = segments.size();
    if (n < 2)
        return result;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            Point2 p;
            if (segmentsIntersect(segments[i], segments[j], nullptr, nullptr, &p, tol)) {
                result.push_back({ p, static_cast<int>(i), static_cast<int>(j) });
            }
        }
    }
    return result;
}

}  // namespace mulan::math
