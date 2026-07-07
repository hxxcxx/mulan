/**
 * @file tool_controller.h
 * @brief 管理当前编辑工具并分发结构化输入。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "editor_tool.h"

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

    void start(std::unique_ptr<EditorTool> tool, ToolContext& context);
    bool handleInput(ToolContext& context, const EditorInput& input);
    void cancel(ToolContext& context);
    void clear(ToolContext& context, ToolFinishReason reason);

private:
    std::unique_ptr<EditorTool> active_tool_;
};

}  // namespace mulan::app
