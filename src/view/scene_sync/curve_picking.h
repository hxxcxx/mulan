/**
 * @file curve_picking.h
 * @brief 曲线拾取：圆/弧的变换与射线求交、采样曲线拾取。
 * @author hxxcxx
 * @date 2026-07-11
 */
#pragma once

#include "picking_types.h"

#include <mulan/asset/curve_asset.h>
#include <mulan/math/math.h>

#include <optional>
#include <vector>

namespace mulan::view::detail {

/// 变换圆到世界坐标系。
math::Circle3 transformCircle(const math::Circle3& circle, const math::Mat4& transform);

/// 变换弧到世界坐标系。
math::Arc3 transformArc(const math::Arc3& arc, const math::Mat4& transform);

/// 圆上离射线最近的点。
std::optional<CurveClosestPoint> closestCirclePointToRay(const math::Ray3& ray, const math::Circle3& circle);

/// 弧上离射线最近的点。
std::optional<CurveClosestPoint> closestArcPointToRay(const math::Ray3& ray, const math::Arc3& arc);

/// 对曲线图元采样后做线段拾取，收集命中候选。
void appendSampledCurvePickCandidate(const math::Ray3& ray, const asset::CurvePrimitive& primitive,
                                     const math::Mat4& worldTransform, size_t elementIndex, double tolerance,
                                     std::vector<MeshPickResult>& out);

}  // namespace mulan::view::detail
