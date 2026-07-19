/**
 * @file importer_registry.h
 * @brief 显式注册 IO 模块内置文件导入器。
 * @author hxxcxx
 * @date 2026-07-19
 */
#pragma once

namespace mulan::io::detail {

/// 幂等注册内置导入器；由 FileManager 构造路径显式触发。
void ensureBuiltinImportersRegistered();

}  // namespace mulan::io::detail
