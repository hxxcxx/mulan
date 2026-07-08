/**
 * @file editor_tool.h
 * @brief 定义视图编辑工具的生命周期和输入响应接口。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "editor_action.h"
#include "editor_input.h"

#include <string_view>

namespace mulan::app {

class EditorTool {
public:
    virtual ~EditorTool() = default;

    virtual std::string_view id() const = 0;
    virtual EditorAction begin() { return EditorAction::ignored(); }
    virtual EditorAction handleInput(const EditorInput& input) = 0;
    virtual EditorAction end(ToolFinishReason reason) {
        (void) reason;
        return EditorAction::ignored();
    }
};

}  // namespace mulan::app
