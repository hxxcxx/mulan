/**
 * @file preview_layer.cpp
 * @brief Transient preview geometry mesh rebuild.
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "preview_layer.h"

#include <mulan/asset/curve_mesh_builder.h>

#include <utility>

namespace mulan::view {

void PreviewLayer::setCurves(std::vector<asset::CurvePrimitive> primitives) {
    curves_ = std::move(primitives);
    rebuild();
}

void PreviewLayer::setCurve(asset::CurvePrimitive primitive) {
    curves_.clear();
    curves_.push_back(std::move(primitive));
    rebuild();
}

void PreviewLayer::clear() {
    if (curves_.empty() && mesh_.empty()) {
        return;
    }

    curves_.clear();
    mesh_ = {};
    touch();
}

void PreviewLayer::rebuild() {
    mesh_ = asset::buildCurveWireMesh(curves_);
    touch();
}

void PreviewLayer::touch() {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
}

void PreviewBuilder::addCurve(asset::CurvePrimitive primitive) {
    curves_.push_back(std::move(primitive));
}

void PreviewBuilder::addSegment(const math::Segment3& segment) {
    addCurve(asset::CurvePrimitive::segment(segment));
}

void PreviewBuilder::addPolyline(const math::Polyline3& polyline) {
    addCurve(asset::CurvePrimitive::polyline(polyline));
}

void PreviewBuilder::addCircle(const math::Circle3& circle) {
    addCurve(asset::CurvePrimitive::circle(circle));
}

void PreviewBuilder::addArc(const math::Arc3& arc) {
    addCurve(asset::CurvePrimitive::arc(arc));
}

std::vector<asset::CurvePrimitive> PreviewBuilder::takeCurves() {
    return std::move(curves_);
}

}  // namespace mulan::view
