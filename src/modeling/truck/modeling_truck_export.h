/**
 * @file modeling_truck_export.h
 * @brief modeling_truck 导出宏。truck 后端对外仅暴露 mulan_load_backend。
 * @author hxxcxx
 * @date 2026-07-10
 */
#pragma once

#include <mulan/core/platform/symbol_visibility.h>

#ifdef BUILDING_MODELING_TRUCK
#define MODELING_TRUCK_API MULAN_SYMBOL_EXPORT
#else
#define MODELING_TRUCK_API MULAN_SYMBOL_IMPORT
#endif
