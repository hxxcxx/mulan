/**
 * @file math_export.h
 * @brief Math 模块导出宏定义
 * @author hxxcxx
 * @date 2026-06-29
 *
 * Math 为 header-only 模块（纯模板/inline），正常使用无需导出符号。
 * 保留宏以与项目其余模块（Core/Geometry）的导出惯例一致。
 */
#pragma once

// math 当前始终以静态库使用，不跨共享库边界导出符号。
#define MATH_API
