/**
 * @file modeling_truck_export.h
 * @brief modeling_truck 导出宏。truck 后端对外仅暴露 mulan_load_backend。
 * @author hxxcxx
 * @date 2026-07-10
 */
#pragma once

#ifdef _WIN32
#ifdef BUILDING_MODELING_TRUCK
#define MODELING_TRUCK_API __declspec(dllexport)
#else
#define MODELING_TRUCK_API __declspec(dllimport)
#endif
#else
#define MODELING_TRUCK_API
#endif
