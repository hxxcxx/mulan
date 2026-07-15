#include "occt_shape_ops.h"
#include "occt_shape_storage.h"
#include "shape_factory.h"

#include <mulan/core/result/error.h>

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

#include <stdexcept>

namespace mulan::modeling {

namespace {

/// 从 OcctShapeStorage 取 TopoDS_Shape;非 OCCT 后端返回错误。
Result<TopoDS_Shape> topoFromShape(const Shape& s) {
    auto storage = storageOf(s);
    if (!storage)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "boolean operand is empty Shape"));
    auto occt = std::dynamic_pointer_cast<OcctShapeStorage>(storage);
    if (!occt)
        return std::unexpected(Error::make(ErrorCode::NotSupported, "boolean operand is not an OCCT shape"));
    return occt->topoShape();
}

/// 把世界坐标点序列构造成 OCCT Wire(闭合多边形)。
TopoDS_Wire loopToWire(std::span<const math::Point3> points) {
    BRepBuilderAPI_MakePolygon poly;
    for (const auto& p : points)
        poly.Add(gp_Pnt(p.x, p.y, p.z));
    poly.Close();
    return poly.Wire();
}

}  // namespace

Result<Shape> OccShapeOps::extrude(const ExtrudeParams& params) {
    if (!params.profile.hasOuterLoop() && !params.circleProfile)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "extrude profile has no outer loop"));
    if (params.distance <= 0.0)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "extrude distance must be positive"));

    // 方向:零向量用 profile.frame.normal。
    math::Vec3 dir = params.direction;
    if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0)
        dir = params.circleProfile ? params.circleProfile->normal : params.profile.frame.normal;
    if (params.inward)
        dir = -dir;

    try {
        TopoDS_Face face;
        if (params.circleProfile) {
            const math::Circle3& circle = *params.circleProfile;
            const gp_Circ occtCircle(gp_Ax2(gp_Pnt(circle.center.x, circle.center.y, circle.center.z),
                                            gp_Dir(circle.normal.x, circle.normal.y, circle.normal.z)),
                                     circle.radius);
            BRepBuilderAPI_MakeEdge edgeMaker(occtCircle);
            BRepBuilderAPI_MakeWire wireMaker(edgeMaker.Edge());
            BRepBuilderAPI_MakeFace faceMaker(wireMaker.Wire());
            if (!faceMaker.IsDone()) {
                return std::unexpected(Error::make(ErrorCode::Internal, "extrude: failed to build circle face"));
            }
            face = faceMaker.Face();
        } else {
            BRepBuilderAPI_MakeFace faceMaker(loopToWire(params.profile.outer.points));
            if (!faceMaker.IsDone()) {
                return std::unexpected(Error::make(ErrorCode::Internal, "extrude: failed to build face"));
            }
            face = faceMaker.Face();
        }

        // 沿方向拉伸
        gp_Vec vec(dir.x * params.distance, dir.y * params.distance, dir.z * params.distance);
        BRepPrimAPI_MakePrism prism(face, vec);
        prism.Build();
        if (!prism.IsDone())
            return std::unexpected(Error::make(ErrorCode::Internal, "extrude: MakePrism failed"));

        return makeShape(prism.Shape());
    } catch (const std::exception& e) {
        return std::unexpected(Error::make(ErrorCode::Internal, e.what()));
    }
}

Result<Shape> OccShapeOps::boolean(const Shape& target, const Shape& tool, BooleanOp op) {
    auto a = topoFromShape(target);
    if (!a)
        return std::unexpected(a.error());
    auto b = topoFromShape(tool);
    if (!b)
        return std::unexpected(b.error());

    if (a->IsNull() || b->IsNull())
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "boolean operand is null"));

    try {
        TopoDS_Shape result;
        switch (op) {
        case BooleanOp::Union: result = BRepAlgoAPI_Fuse(*a, *b).Shape(); break;
        case BooleanOp::Difference: result = BRepAlgoAPI_Cut(*a, *b).Shape(); break;
        case BooleanOp::Intersection: result = BRepAlgoAPI_Common(*a, *b).Shape(); break;
        }
        if (result.IsNull())
            return std::unexpected(Error::make(ErrorCode::Internal, "boolean produced null result"));
        return makeShape(result);
    } catch (const std::exception& e) {
        return std::unexpected(Error::make(ErrorCode::Internal, e.what()));
    }
}

}  // namespace mulan::modeling
