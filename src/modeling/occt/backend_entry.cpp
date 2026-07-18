// OCCT 后端 C 加载入口：由 runtime 经 LoadLibrary + GetProcAddress 调用。
// 所有 OCCT 类不导出；本符号是 DLL 对外的唯一契约。
#include "step_reader.h"
#include "occt_shape_ops.h"
#include "modeling_occt_export.h"
#include <mulan/modeling/core/shape_ops.h>

#include <memory>

extern "C" {

/// 把 OCCT 后端的所有能力注册进 modeling_core 的中立注册表。
MODELING_OCCT_API void mulan_load_backend() {
    // 文件读取始终由 OCCT 提供，不受 ShapeOps 后端选择影响。
    mulan::modeling::registerOccStepReader();
    mulan::modeling::ShapeOpsRegistry::instance().registerOps("occt", std::make_unique<mulan::modeling::OccShapeOps>());
}

}  // extern "C"
