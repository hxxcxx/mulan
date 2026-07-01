/**
 * @file geo_export.h
 * @brief Geo 模块导出宏定义
 * @author hxxcxx
 * @date 2026-06-29
 *
 * Geo 为 header-only 模块（纯模板/inline），正常使用无需导出符号。
 * 保留宏以与项目其余模块（Core/Geometry）的导出惯例一致。
 */
#pragma once

#ifdef BUILDING_GEO
    #define GEO_API __declspec(dllexport)
#elif defined(USING_GEO_DLL)
    #define GEO_API __declspec(dllimport)
#else
    #define GEO_API
#endif
