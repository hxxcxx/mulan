#include "shape_factory.h"
#include "occt_shape_storage.h"

namespace mulan::modeling {

Shape makeShape(const TopoDS_Shape& shape) {
    if (shape.IsNull())
        return Shape{};
    auto storage = std::make_shared<OcctShapeStorage>(shape);
    return makeShapeFromStorage(std::move(storage));
}

}  // namespace mulan::modeling
