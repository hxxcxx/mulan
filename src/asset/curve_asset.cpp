/**
 * @file curve_asset.cpp
 * @brief Curve asset storage and render mesh rebuild.
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "curve_asset.h"

#include "curve_mesh_builder.h"

#include <algorithm>
#include <span>

namespace mulan::asset {
namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

}  // namespace

CurvePrimitive CurvePrimitive::segment(const math::Segment3& segment) {
    return CurvePrimitive(CurveSegmentPrimitive{ segment });
}

CurvePrimitive CurvePrimitive::polyline(const math::Polyline3& polyline) {
    return CurvePrimitive(CurvePolylinePrimitive{ polyline });
}

CurvePrimitive CurvePrimitive::circle(const math::Circle3& circle) {
    return CurvePrimitive(CurveCirclePrimitive{ circle });
}

CurvePrimitive CurvePrimitive::arc(const math::Arc3& arc) {
    return CurvePrimitive(CurveArcPrimitive{ arc });
}

CurvePrimitive CurvePrimitive::bezier(const math::BezierCurve3d& curve) {
    return CurvePrimitive(CurveBezierPrimitive{ curve });
}

CurvePrimitive CurvePrimitive::bspline(const math::BSplineCurve3d& curve) {
    return CurvePrimitive(CurveBSplinePrimitive{ curve });
}

CurvePrimitive CurvePrimitive::nurbs(const math::NURBSCurve3d& curve) {
    return CurvePrimitive(CurveNurbsPrimitive{ curve });
}

CurveElementKind CurvePrimitive::kind() const {
    return std::visit(Overloaded{
                              [](const CurveSegmentPrimitive&) { return CurveElementKind::Segment; },
                              [](const CurvePolylinePrimitive&) { return CurveElementKind::Polyline; },
                              [](const CurveCirclePrimitive&) { return CurveElementKind::Circle; },
                              [](const CurveArcPrimitive&) { return CurveElementKind::Arc; },
                              [](const CurveBezierPrimitive&) { return CurveElementKind::Bezier; },
                              [](const CurveBSplinePrimitive&) { return CurveElementKind::BSpline; },
                              [](const CurveNurbsPrimitive&) { return CurveElementKind::NURBS; },
                      },
                      data_);
}

CurveElementId CurveAsset::add(CurvePrimitive primitive) {
    CurveElement element;
    element.id = allocateElementId();
    element.primitive = std::move(primitive);
    elements_.push_back(std::move(element));
    rebuildRenderMesh();
    return elements_.back().id;
}

bool CurveAsset::update(CurveElementId id, CurvePrimitive primitive) {
    for (auto& element : elements_) {
        if (element.id != id) {
            continue;
        }
        element.primitive = std::move(primitive);
        rebuildRenderMesh();
        return true;
    }
    return false;
}

bool CurveAsset::remove(CurveElementId id) {
    const auto oldSize = elements_.size();
    elements_.erase(std::remove_if(elements_.begin(), elements_.end(),
                                   [id](const CurveElement& element) { return element.id == id; }),
                    elements_.end());
    if (elements_.size() == oldSize) {
        return false;
    }
    rebuildRenderMesh();
    return true;
}

CurveElementId CurveAsset::addSegment(const math::Segment3& segment) {
    return add(CurvePrimitive::segment(segment));
}

bool CurveAsset::updateSegment(CurveElementId id, const math::Segment3& segment) {
    return update(id, CurvePrimitive::segment(segment));
}

void CurveAsset::collectDrawables(std::vector<Drawable>& out) const {
    for (const graphics::Mesh& mesh : element_wire_meshes_) {
        // 保留空元素占位，确保 drawable index 与 CurveElement 索引稳定对齐。
        out.push_back({ &mesh, AssetId::invalid(), DrawableRole::Wire });
    }
}

math::AABB3 CurveAsset::localBounds() const {
    return wire_mesh_.bounds;
}

void CurveAsset::rebuildRenderMesh() {
    wire_mesh_ = buildCurveWireMesh(elements_);
    element_wire_meshes_.clear();
    element_wire_meshes_.reserve(elements_.size());
    for (const CurveElement& element : elements_) {
        element_wire_meshes_.push_back(buildCurveWireMesh(std::span<const CurveElement>(&element, 1)));
    }
}

CurveElementId CurveAsset::allocateElementId() {
    CurveElementId id = next_element_id_;
    ++next_element_id_.value;
    return id;
}

}  // namespace mulan::asset
