/**
 * @file modeling_occt_export.h
 * @brief modeling_occt 导出宏。当前为静态库，宏为空；预留 DLL 化。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#ifdef BUILDING_MODELING_OCCT
#define MODELING_OCCT_API __declspec(dllexport)
#elif defined(BUILDING_MODELING_OCCT_DLL)
#define MODELING_OCCT_API __declspec(dllimport)
#else
#define MODELING_OCCT_API
#endif
