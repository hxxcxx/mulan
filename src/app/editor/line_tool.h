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

    EditorAction begin() override;
    EditorAction handleInput(const EditorInput& input) override;
    EditorAction end(ToolFinishReason reason) override;

private:
    enum class Step {
        FirstPoint,
        SecondPoint,
    };

    EditorAction acceptPoint(const math::Point3& point);
    EditorAction updatePreview(const math::Point3& point) const;

    Step step_ = Step::FirstPoint;
    std::optional<math::Point3> first_point_;
};

}  // namespace mulan::app
