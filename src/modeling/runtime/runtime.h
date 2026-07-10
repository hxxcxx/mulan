/**
 * @file runtime.h
 * @brief 通用组装层入口：在程序启动时把各建模后端接入 modeling_core。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * runtime 是项目的胶水层：依赖 modeling_core + 各具体后端（modeling_occt 等），
 * 在 init() 时完成注册/资源装配。调用方（app）只依赖本头，不接触任何后端，
 * 因此后端对 io/asset/app 源码不可见。
 *
 * 后端实例在此被显式引用，故其对象代码必然被链接进最终可执行文件，
 * 无需 WHOLE_ARCHIVE 或静态对象自注册。
 */
#pragma once

#include "runtime_export.h"

namespace mulan::runtime {

/// 初始化组装层：注册建模后端、装配资源。app 启动时调用一次，在打开文档前。
RUNTIME_API void init();

}  // namespace mulan::runtime
