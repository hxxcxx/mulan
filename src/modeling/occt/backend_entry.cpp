// OCCT 后端 C 加载入口：由 runtime 经 LoadLibrary + GetProcAddress 调用。
// 所有 OCCT 类不导出；本符号是 DLL 对外的唯一契约。
#include "step_reader.h"

extern "C" {

/// 把 OCCT 后端的 STEP/IGES 读取器注册进 modeling_core 的中立注册表。
__declspec(dllexport) void mulan_load_backend() {
    mulan::modeling::registerOccStepReader();
}

}  // extern "C"
