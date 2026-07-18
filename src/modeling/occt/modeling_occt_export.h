/**
 * @file modeling_occt_export.h
 * @brief modeling_occt 动态插件的跨平台符号导出宏。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/core/platform/symbol_visibility.h>

#ifdef BUILDING_MODELING_OCCT
#define MODELING_OCCT_API MULAN_SYMBOL_EXPORT
#elif defined(BUILDING_MODELING_OCCT_DLL)
#define MODELING_OCCT_API MULAN_SYMBOL_IMPORT
#else
#define MODELING_OCCT_API
#endif
