/**
 * @file command_export.h
 * @brief Command 模块导出宏定义
 * @author hxxcxx
 * @date 2026-05-25
 *
 * Command 是静态库，不需要 dllexport/dllimport。
 * 保留宏名以便将来切换为 DLL。
 */
#pragma once

#define COMMAND_API
