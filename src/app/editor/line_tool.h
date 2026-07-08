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
    enum class State {
        AwaitingStart,
        RubberBand,
    };

    EditorAction acceptStartPoint(const math::Point3& point);
    EditorAction acceptEndPoint(const math::Point3& point);
    EditorAction updateRubberBand(const math::Point3& point);

    State state_ = State::AwaitingStart;
    std::optional<math::Point3> first_point_;
    std::optional<math::Point3> current_point_;
};

}  // namespace mulan::app
