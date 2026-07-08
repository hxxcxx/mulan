/**
 * @file draft_geometry.h
 * @brief 定义编辑过程中的临时几何。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include <mulan/asset/curve_asset.h>

#include <utility>
#include <vector>

namespace mulan::app {

class DraftGeometry {
public:
    static DraftGeometry curve(asset::CurvePrimitive primitive);
    static DraftGeometry segment(const math::Segment3& segment);

    bool empty() const { return curves_.empty(); }
    const std::vector<asset::CurvePrimitive>& curves() const { return curves_; }
    std::vector<asset::CurvePrimitive> takeCurves() { return std::move(curves_); }

private:
    explicit DraftGeometry(std::vector<asset::CurvePrimitive> curves) : curves_(std::move(curves)) {}

    std::vector<asset::CurvePrimitive> curves_;
};

}  // namespace mulan::app
