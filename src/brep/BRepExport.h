/**
 * @file BRepExport.h
 * @brief BRep 模块导出宏定义（动态库）
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#ifdef BUILDING_BREP
    #define BREP_API __declspec(dllexport)
#else
    #define BREP_API __declspec(dllimport)
#endif
