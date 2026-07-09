#include "occt_shape_storage.h"
#include "occt_tessellator_internal.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopAbs.hxx>

namespace mulan::modeling {

OcctShapeStorage::OcctShapeStorage(TopoDS_Shape shape) : shape_(std::move(shape)) {
}

BodyKind OcctShapeStorage::bodyKind() const {
    if (shape_.IsNull())
        return BodyKind::Empty;
    switch (shape_.ShapeType()) {
    case TopAbs_COMPOUND: return BodyKind::Compound;
    case TopAbs_COMPSOLID:
    case TopAbs_SOLID: return BodyKind::Solid;
    case TopAbs_SHELL:
    case TopAbs_FACE: return BodyKind::Sheet;
    case TopAbs_WIRE:
    case TopAbs_EDGE: return BodyKind::Wire;
    default: return BodyKind::Empty;
    }
}

math::AABB3 OcctShapeStorage::bounds() const {
    if (shape_.IsNull())
        return math::AABB3::empty();

    Bnd_Box box;
    BRepBndLib::Add(shape_, box);
    if (box.IsVoid())
        return math::AABB3::empty();

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    math::AABB3 result;
    result.min = { xmin, ymin, zmin };
    result.max = { xmax, ymax, zmax };
    return result;
}

core::Result<TessellatedGeometry> OcctShapeStorage::tessellate(const TessellationOptions& opts) const {
    return detail::tessellateTopoShape(shape_, opts);
}

}  // namespace mulan::modeling
