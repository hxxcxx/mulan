#include "truck_shape_ops.h"
#include "modeling_truck_export.h"

#include <mulan/modeling/core/shape_ops.h>

#include <memory>

extern "C" {

MODELING_TRUCK_API void mulan_load_backend() {
    // 仅注册建模操作。文件读写能力固定由 OCCT 插件提供。
    mulan::modeling::ShapeOpsRegistry::instance().registerOps("truck",
                                                              std::make_unique<mulan::modeling::TruckShapeOps>());
}

}  // extern "C"
