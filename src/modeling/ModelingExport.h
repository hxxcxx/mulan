/**
 * @file ModelingExport.h
 * @brief Modeling 模块导出宏定义（动态库）
 * @author hxxcxx
 * @date 2026-05-18
 */
#pragma once

#ifdef BUILDING_MODELING
    #define MODELING_API __declspec(dllexport)
#else
    #define MODELING_API __declspec(dllimport)
#endif
