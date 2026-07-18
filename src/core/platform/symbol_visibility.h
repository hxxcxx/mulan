/**
 * @file symbol_visibility.h
 * @brief 跨编译器的共享库符号可见性定义
 * @author hxxcxx
 * @date 2026-07-18
 *
 * 模块自己的导出头负责判断当前是导出方还是使用方；本文件只封装
 * 编译器语法，避免各模块分别散落 __declspec 或 visibility 属性。
 */

#pragma once

#if defined(_WIN32)
#define MULAN_SYMBOL_EXPORT __declspec(dllexport)
#define MULAN_SYMBOL_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define MULAN_SYMBOL_EXPORT __attribute__((visibility("default")))
#define MULAN_SYMBOL_IMPORT __attribute__((visibility("default")))
#else
#define MULAN_SYMBOL_EXPORT
#define MULAN_SYMBOL_IMPORT
#endif
