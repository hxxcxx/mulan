/**
 * @file modeling_core_export.h
 * @brief modeling_core 导出宏。当前为静态库，宏为空；预留 DLL 化。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/core/platform/symbol_visibility.h>

#ifdef BUILDING_MODELING_CORE
#define MODELING_CORE_API MULAN_SYMBOL_EXPORT
#elif defined(BUILDING_MODELING_CORE_DLL)
#define MODELING_CORE_API MULAN_SYMBOL_IMPORT
#else
#define MODELING_CORE_API
#endif
