/**
 * @file line_tool.h
 * @brief 定义两点线段绘制工具。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include "editor_tool.h"

#include <optional>

namespace mulan::app {

class LineTool final : public EditorTool {
public:
    std::string_view id() const override { return "draw.line"; }

    void begin(ToolContext& context) override;
    ToolInputResult handleInput(ToolContext& context, const EditorInput& input) override;
    void end(ToolContext& context, ToolFinishReason reason) override;

private:
    enum class Step {
        FirstPoint,
        SecondPoint,
    };

    ToolInputResult acceptPoint(ToolContext& context, const math::Point3& point);
    void updatePreview(ToolContext& context, const math::Point3& point);

    Step step_ = Step::FirstPoint;
    std::optional<math::Point3> first_point_;
};

}  // namespace mulan::app
