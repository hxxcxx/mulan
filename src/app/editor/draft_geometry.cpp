/**
 * @file draft_geometry.cpp
 * @brief DraftGeometry 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "draft_geometry.h"

namespace mulan::app {

DraftGeometry DraftGeometry::curve(asset::CurvePrimitive primitive) {
    std::vector<asset::CurvePrimitive> curves;
    curves.push_back(std::move(primitive));
    return DraftGeometry(std::move(curves));
}

DraftGeometry DraftGeometry::segment(const math::Segment3& segment) {
    return curve(asset::CurvePrimitive::segment(segment));
}

}  // namespace mulan::app
