/**
 * @file Export.h
 * @brief Geometry 模块导出宏定义（动态库）
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#ifdef BUILDING_GEOMETRY
    #define GEOMETRY_API __declspec(dllexport)
#else
    #define GEOMETRY_API __declspec(dllimport)
#endif
