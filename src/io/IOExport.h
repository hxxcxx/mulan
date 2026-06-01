/**
 * @file IOExport.h
 * @brief IO 模块导出宏定义
 * @author hxxcxx
 * @date 2026-04-22
 *
 * IO 是静态库，不需要 dllexport/dllimport。
 * 保留宏名以便将来切换为 DLL。
 */
#pragma once

#define IO_API
