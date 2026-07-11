/**
 * @file builtin_commands.h
 * @brief 注册应用内置命令。
 *
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

namespace mulan::app {

class CommandManager;

void registerBuiltinCommands(CommandManager& manager);

}  // namespace mulan::app
