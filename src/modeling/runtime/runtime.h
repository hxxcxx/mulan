/**
 * @file runtime.h
 * @brief 通用组装层入口：在程序启动时把各建模后端接入 modeling_core。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * runtime 是项目的胶水层：扫描并加载各后端插件，在 init() 时完成注册/资源装配。
 * 文件 reader 固定由 OCCT 插件提供；IShapeOps 的默认实现由 CMake 变量
 * MULAN_DEFAULT_SHAPE_OPS_BACKEND 决定，并可通过 MULAN_SHAPE_OPS_BACKEND
 * 临时覆盖。调用方（app）只依赖本头，不接触任何后端。
 *
 * 兼容旧配置 MULAN_MODELING_BACKEND，但新代码应使用职责更明确的变量名。
 */
#pragma once

#include "runtime_export.h"

namespace mulan::runtime {

/// 初始化组装层：注册建模后端、装配资源。app 启动时调用一次，在打开文档前。
RUNTIME_API void init();

}  // namespace mulan::runtime
