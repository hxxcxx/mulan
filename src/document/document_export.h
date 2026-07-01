/**
 * @file document_export.h
 * @brief Document 模块导出宏定义
 * @author hxxcxx
 * @date 2026-06-30
 *
 * Document 为静态库，正常使用无需导出符号。
 * 保留宏以与项目其余模块的导出惯例一致。
 */
#pragma once

#ifdef BUILDING_DOCUMENT
    #define DOCUMENT_API __declspec(dllexport)
#elif defined(USING_DOCUMENT_DLL)
    #define DOCUMENT_API __declspec(dllimport)
#else
    #define DOCUMENT_API
#endif
