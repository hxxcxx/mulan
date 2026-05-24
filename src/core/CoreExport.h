/**
 * @file CoreExport.h
 * @brief Core 模块导出宏定义（动态库）
 * @author hxxcxx
 * @date 2026-04-23
 */
#pragma once

#ifdef BUILDING_CORE
    #define CORE_API __declspec(dllexport)
#elif defined(BUILDING_CORE_DLL)
    #define CORE_API __declspec(dllimport)
#else
    #define CORE_API
#endif

