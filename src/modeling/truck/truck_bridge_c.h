/**
 * @file truck_bridge_c.h
 * @brief truck-bridge 自动生成 C 头的 C++ 引入包装。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * cbindgen 生成的 truck_bridge.h 当前不包 extern "C"。C++ 后端统一包含本头，
 * 保证链接时按 C ABI 查找 truck_bridge.dll 导出的符号。
 */
#pragma once

extern "C" {
#include <truck_bridge.h>
}
