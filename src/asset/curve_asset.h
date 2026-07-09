/**
 * @file curve_asset.h
 * @brief 可编辑的结构化曲线资产，并派生渲染线框网格。
 * @author hxxcxx
 * @date 2026-07-07
 *
 * CurveAsset 保存命令与编辑语义。渲染 mesh 是从结构化曲线图元重建出的
 * 派生线框表示，不能作为长期编辑数据源。
 */
#pragma once

#include "geometry_asset.h"

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mulan::asset {

enum class CurveElementKind : uint8_t {
    Segment,
    Polyline,
    Circle,
    Arc,
    Bezier,
    BSpline,
    NURBS,
};

struct CurveElementId {
    uint32_t value = 0;

    static constexpr CurveElementId invalid() { return {}; }
    constexpr bool valid() const { return value != 0; }

    friend constexpr bool operator==(CurveElementId a, CurveElementId b) { return a.value == b.value; }
    friend constexpr bool operator!=(CurveElementId a, CurveElementId b) { return !(a == b); }
};

struct CurveSegmentPrimitive {
    math::Segment3 segment;
};

struct CurvePolylinePrimitive {
    math::Polyline3 polyline;
};

struct CurveCirclePrimitive {
    math::Circle3 circle;
};

struct CurveArcPrimitive {
    math::Arc3 arc;
};

struct CurveBezierPrimitive {
    math::BezierCurve3d curve;
};

struct CurveBSplinePrimitive {
    math::BSplineCurve3d curve;
};

struct CurveNurbsPrimitive {
    math::NURBSCurve3d curve;
};

using CurvePrimitiveData =
        std::variant<CurveSegmentPrimitive, CurvePolylinePrimitive, CurveCirclePrimitive, CurveArcPrimitive,
                     CurveBezierPrimitive, CurveBSplinePrimitive, CurveNurbsPrimitive>;

class CurvePrimitive {
public:
    CurvePrimitive() = default;

    static CurvePrimitive segment(const math::Segment3& segment);
    static CurvePrimitive polyline(const math::Polyline3& polyline);
    static CurvePrimitive circle(const math::Circle3& circle);
    static CurvePrimitive arc(const math::Arc3& arc);
    static CurvePrimitive bezier(const math::BezierCurve3d& curve);
    static CurvePrimitive bspline(const math::BSplineCurve3d& curve);
    static CurvePrimitive nurbs(const math::NURBSCurve3d& curve);

    CurveElementKind kind() const;

    const CurvePrimitiveData& data() const { return data_; }
    CurvePrimitiveData& data() { return data_; }

private:
    explicit CurvePrimitive(CurvePrimitiveData data) : data_(std::move(data)) {}

    CurvePrimitiveData data_;
};

struct CurveElement {
    CurveElementId id;
    CurvePrimitive primitive;
};

class CurveAsset : public GeometryAsset {
public:
    explicit CurveAsset(AssetId id, std::string name) : GeometryAsset(id, AssetKind::Curve, std::move(name)) {}

    CurveElementId add(CurvePrimitive primitive);
    bool update(CurveElementId id, CurvePrimitive primitive);
    bool remove(CurveElementId id);
    void setElements(std::vector<CurveElement> elements);

    CurveElementId addSegment(const math::Segment3& segment);
    bool updateSegment(CurveElementId id, const math::Segment3& segment);

    const std::vector<CurveElement>& elements() const { return elements_; }

    void collectDrawables(std::vector<Drawable>& out) const override;
    math::AABB3 localBounds() const override;

private:
    void rebuildRenderMesh();

    CurveElementId allocateElementId();

    CurveElementId next_element_id_{ 1 };
    std::vector<CurveElement> elements_;
    graphics::Mesh wire_mesh_;
    std::vector<graphics::Mesh> element_wire_meshes_;
};

}  // namespace mulan::asset
