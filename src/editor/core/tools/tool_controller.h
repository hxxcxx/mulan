/**
 * @file tool_controller.h
 * @brief 管理当前编辑工具并分发结构化输入。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "core/tools/editor_tool.h"

#include <memory>

namespace mulan::app {

class ToolController {
public:
    ~ToolController();

    ToolController(const ToolController&) = delete;
    ToolController& operator=(const ToolController&) = delete;

    ToolController() = default;

    bool hasActiveTool() const { return active_tool_ != nullptr; }
    EditorTool* activeTool() const { return active_tool_.get(); }

    EditorAction start(std::unique_ptr<EditorTool> tool);
    EditorAction handleInput(const EditorInput& input);
    EditorAction cancel();
    EditorAction clear(ToolFinishReason reason);

private:
    std::unique_ptr<EditorTool> active_tool_;
};

}  // namespace mulan::app
