/**
 * @file Command.h
 * @brief 命令抽象基类
 * @author hxxcxx
 * @date 2026-05-25
 *
 * 设计思路：
 *  - Command 是对 Document 操作的抽象单元
 *  - execute() 接收 Document 引用，不持有指针
 *  - 命令不关心 undo/redo（由 Document 快照机制负责）
 *  - 不依赖 Qt / engine，只依赖 document 数据模型
 */
#pragma once

#include "CommandExport.h"

#include <string>

namespace mulan::document {
class Document;
}

namespace mulan::command {

class COMMAND_API Command {
public:
    virtual ~Command() = default;

    /// 执行命令，返回是否成功
    virtual bool execute(document::Document& doc) = 0;

    /// 命令名称（用于日志、调试、UI 显示）
    virtual std::string name() const = 0;
};

} // namespace mulan::command
