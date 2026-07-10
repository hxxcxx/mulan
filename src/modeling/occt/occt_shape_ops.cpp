#include "occt_shape_ops.h"
#include "occt_shape_storage.h"
#include "shape_factory.h"

#include <mulan/core/result/error.h>

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <stdexcept>

namespace mulan::modeling {

namespace {

/// 从 OcctShapeStorage 取 TopoDS_Shape;非 OCCT 后端返回错误。
core::Result<TopoDS_Shape> topoFromShape(const Shape& s) {
    auto storage = storageOf(s);
    if (!storage)
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "boolean operand is empty Shape"));
    auto occt = std::dynamic_pointer_cast<OcctShapeStorage>(storage);
    if (!occt)
        return std::unexpected(
                core::Error::make(core::ErrorCode::NotSupported, "boolean operand is not an OCCT shape"));
    return occt->topoShape();
}

/// 把 FaceLoop(世界坐标点)构造成 OCCT Wire(闭合多边形)。
TopoDS_Wire loopToWire(const asset::FaceLoop& loop) {
    BRepBuilderAPI_MakePolygon poly;
    for (const auto& p : loop.points)
        poly.Add(gp_Pnt(p.x, p.y, p.z));
    poly.Close();
    return poly.Wire();
}

}  // namespace

core::Result<Shape> OccShapeOps::extrude(const ExtrudeParams& params) {
    if (!params.profile.hasOuterLoop())
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "extrude profile has no outer loop"));
    if (params.distance <= 0.0)
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "extrude distance must be positive"));

    // 方向:零向量用 frame.normal。
    math::Vec3 dir = params.direction;
    if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0)
        dir = params.profile.frame.normal;
    if (params.inward)
        dir = -dir;

    try {
        // 外环 Wire → Face(本轮先只处理外环;holes 后续)
        TopoDS_Wire outerWire = loopToWire(params.profile.outer);
        BRepBuilderAPI_MakeFace faceMaker(outerWire);
        if (!faceMaker.IsDone())
            return std::unexpected(core::Error::make(core::ErrorCode::Internal, "extrude: failed to build face"));
        TopoDS_Face face = faceMaker.Face();

        // 沿方向拉伸
        gp_Vec vec(dir.x * params.distance, dir.y * params.distance, dir.z * params.distance);
        BRepPrimAPI_MakePrism prism(face, vec);
        prism.Build();
        if (!prism.IsDone())
            return std::unexpected(core::Error::make(core::ErrorCode::Internal, "extrude: MakePrism failed"));

        return makeShape(prism.Shape());
    } catch (const std::exception& e) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, e.what()));
    }
}

core::Result<Shape> OccShapeOps::boolean(const Shape& target, const Shape& tool, BooleanOp op) {
    auto a = topoFromShape(target);
    if (!a)
        return std::unexpected(a.error());
    auto b = topoFromShape(tool);
    if (!b)
        return std::unexpected(b.error());

    if (a->IsNull() || b->IsNull())
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "boolean operand is null"));

    try {
        TopoDS_Shape result;
        switch (op) {
        case BooleanOp::Union: result = BRepAlgoAPI_Fuse(*a, *b).Shape(); break;
        case BooleanOp::Difference: result = BRepAlgoAPI_Cut(*a, *b).Shape(); break;
        case BooleanOp::Intersection: result = BRepAlgoAPI_Common(*a, *b).Shape(); break;
        }
        if (result.IsNull())
            return std::unexpected(core::Error::make(core::ErrorCode::Internal, "boolean produced null result"));
        return makeShape(result);
    } catch (const std::exception& e) {
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, e.what()));
    }
}

}  // namespace mulan::modeling
