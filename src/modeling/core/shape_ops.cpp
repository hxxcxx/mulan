#include "shape_ops.h"

namespace mulan::modeling {

ShapeOpsRegistry& ShapeOpsRegistry::instance() {
    static ShapeOpsRegistry registry;
    return registry;
}

void ShapeOpsRegistry::registerOps(std::unique_ptr<IShapeOps> ops) {
    ops_ = std::move(ops);
}

IShapeOps* ShapeOpsRegistry::ops() const {
    return ops_.get();
}

}  // namespace mulan::modeling
