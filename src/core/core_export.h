/**
 * @file core_export.h
 * @brief Core 模块导出宏定义（动态库）
 * @author hxxcxx
 * @date 2026-04-23
 */
#pragma once

#include "platform/symbol_visibility.h"

#ifdef BUILDING_CORE
#define CORE_API MULAN_SYMBOL_EXPORT
#elif defined(BUILDING_CORE_DLL)
#define CORE_API MULAN_SYMBOL_IMPORT
#else
#define CORE_API
#endif
