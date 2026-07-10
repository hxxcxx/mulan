#include "truck_shape_ops.h"

#include <mulan/modeling/core/shape_ops.h>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

bool truckBackendRequested() {
    const char* backend = std::getenv("MULAN_MODELING_BACKEND");
    return backend && std::strcmp(backend, "truck") == 0;
}

}  // namespace

extern "C" {

__declspec(dllexport) void mulan_load_backend() {
    if (!truckBackendRequested())
        return;

    mulan::modeling::ShapeOpsRegistry::instance().registerOps(std::make_unique<mulan::modeling::TruckShapeOps>());
}

}  // extern "C"
