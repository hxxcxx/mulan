#include "shape.h"

namespace mulan::modeling {

Shape::Shape() = default;

Shape::~Shape() = default;

Shape::Shape(const Shape&) = default;

Shape& Shape::operator=(const Shape&) = default;

Shape::Shape(Shape&&) noexcept = default;

Shape& Shape::operator=(Shape&&) noexcept = default;

BodyKind Shape::bodyKind() const {
    return storage_ ? storage_->bodyKind() : BodyKind::Empty;
}

math::AABB3 Shape::bounds() const {
    return storage_ ? storage_->bounds() : math::AABB3::empty();
}

Result<TessellatedGeometry> Shape::tessellate(const TessellationOptions& opts) const {
    if (!storage_)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "tessellate on empty Shape"));
    return storage_->tessellate(opts);
}

}  // namespace mulan::modeling
