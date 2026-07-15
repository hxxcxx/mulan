/**
 * @file editor_action_tests.cpp
 * @brief EditorAction 语义与 ToolController end action 合并测试。
 *
 * 不依赖 GPU / Qt。link editor 库但不调用任何渲染/视图初始化。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <gtest/gtest.h>

#include <mulan/editor/core/operation/editor_action.h>
#include <mulan/editor/core/operation/document_operation.h>
#include <mulan/editor/core/tools/editor_tool.h>
#include <mulan/editor/core/tools/tool_controller.h>
#include <mulan/editor/core/selection/editor_input.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <optional>

using namespace mulan::editor;
using namespace mulan::engine;
using mulan::scene::EntityId;

namespace {

/// 构造一个最小 DocumentOperation（removeEntities 不依赖 asset/math 几何构造）。
DocumentOperation makeDummyOperation() {
    return DocumentOperation::removeEntities({}, false);
}

/// Fake tool：begin/handleInput/end 可由测试预设返回值。
/// end 被调用时把 reason 写入外部 optional（因为 clear() 后 tool 会被析构，
/// 裸指针读取成员是 UAF）。
class FakeTool : public EditorTool {
public:
    FakeTool(std::string_view id, std::optional<ToolFinishReason>& endReasonSink)
        : id_(id), endReasonSink_(endReasonSink) {}

    std::string_view id() const override { return id_; }

    EditorAction begin() override { return EditorAction::ignored(); }

    EditorAction handleInput(const EditorInput&) override {
        return handle_action ? *handle_action : EditorAction::ignored();
    }

    EditorAction end(ToolFinishReason reason) override {
        endReasonSink_ = reason;  // 写入外部 sink，tool 析构后仍可读
        return end_action ? *end_action : EditorAction::ignored();
    }

    std::string id_;
    std::optional<EditorAction> handle_action;
    std::optional<EditorAction> end_action;

private:
    std::optional<ToolFinishReason>& endReasonSink_;
};

EditorInput makeInput(InputEvent event) {
    EditorInput input;
    input.event = std::move(event);
    return input;
}

}  // namespace

// ============================================================
// EditorAction::commitAndFinish 语义
// ============================================================

TEST(EditorActionCommitAndFinish, SetsConsumedAndOperation) {
    EditorAction action = EditorAction::commitAndFinish(makeDummyOperation());
    EXPECT_TRUE(action.isConsumed());
    EXPECT_TRUE(action.operation().has_value());
}

TEST(EditorActionCommitAndFinish, SetsClearPreview) {
    EditorAction action = EditorAction::commitAndFinish(makeDummyOperation());
    EXPECT_TRUE(action.shouldClearPreview());
}

TEST(EditorActionCommitAndFinish, SetsFinishedLifecycle) {
    EditorAction action = EditorAction::commitAndFinish(makeDummyOperation());
    EXPECT_EQ(action.lifecycle(), ToolLifecycle::Finished);
}

TEST(EditorActionCommitAndFinish, EquivalentToChain) {
    // commitAndFinish(op) 应等价于 commit(op).clearPreviewOnApply()（finishTool 已删除，
    // commitAndFinish 直接设 lifecycle=Finished）。
    EditorAction chained = EditorAction::commit(makeDummyOperation());
    chained.clearPreviewOnApply();
    // 注：commitAndFinish 额外设 lifecycle=Finished，链式版本需要手动设——这正是抽它的原因。

    EditorAction factory = EditorAction::commitAndFinish(makeDummyOperation());
    EXPECT_EQ(factory.isConsumed(), chained.isConsumed());
    EXPECT_EQ(factory.shouldClearPreview(), chained.shouldClearPreview());
    EXPECT_EQ(factory.lifecycle(), ToolLifecycle::Finished);
}

// ============================================================
// ToolController —— end action 不再丢弃（P8 修复）
// ============================================================

// start(新工具) 应保留旧工具 end(Replaced) 的 clearPreview 语义。
TEST(ToolControllerEndAction, StartPreservesReplacedEndClearPreview) {
    ToolController controller;

    std::optional<ToolFinishReason> firstEndReason;
    auto first = std::make_unique<FakeTool>("first", firstEndReason);
    first->end_action = EditorAction::clearPreview();  // end(Replaced) 想清预览
    controller.start(std::move(first));

    std::optional<ToolFinishReason> secondEndReason;
    auto second = std::make_unique<FakeTool>("second", secondEndReason);
    EditorAction action = controller.start(std::move(second));

    // 旧工具的 end 被调用，且 reason=Replaced
    ASSERT_TRUE(firstEndReason.has_value());
    EXPECT_EQ(*firstEndReason, ToolFinishReason::Replaced);
    // 旧工具 end 的 clearPreview 应被合并进 start 返回的 action
    EXPECT_TRUE(action.shouldClearPreview());
}

// handleInput 在工具返回 Finished 时应调用 end(Finished)。
TEST(ToolControllerEndAction, FinishedCallsEnd) {
    ToolController controller;
    std::optional<ToolFinishReason> endReason;
    auto tool = std::make_unique<FakeTool>("tool", endReason);
    tool->handle_action = EditorAction::commitAndFinish(makeDummyOperation());  // lifecycle=Finished
    controller.start(std::move(tool));

    EditorAction action = controller.handleInput(makeInput(
            InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::Left)));

    // end 被调用，reason=Finished
    ASSERT_TRUE(endReason.has_value());
    EXPECT_EQ(*endReason, ToolFinishReason::Finished);
    // 工具已结束
    EXPECT_FALSE(controller.hasActiveTool());
}

// handleInput 在工具返回 Cancelled 时应调用 end(Cancelled)。
TEST(ToolControllerEndAction, CancelledCallsEnd) {
    ToolController controller;
    std::optional<ToolFinishReason> endReason;
    auto tool = std::make_unique<FakeTool>("tool", endReason);
    tool->handle_action = EditorAction::cancel();  // lifecycle=Cancelled
    controller.start(std::move(tool));

    controller.handleInput(makeInput(
            InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::Left)));

    ASSERT_TRUE(endReason.has_value());
    EXPECT_EQ(*endReason, ToolFinishReason::Cancelled);
    EXPECT_FALSE(controller.hasActiveTool());
}

// Escape 走 cancel() 路径，调用 end(Cancelled)。
TEST(ToolControllerEndAction, EscapeCallsCancel) {
    ToolController controller;
    std::optional<ToolFinishReason> endReason;
    auto tool = std::make_unique<FakeTool>("tool", endReason);
    controller.start(std::move(tool));

    EditorAction action = controller.handleInput(makeInput(InputEvent::keyPress(Key::Escape)));

    ASSERT_TRUE(endReason.has_value());
    EXPECT_EQ(*endReason, ToolFinishReason::Cancelled);
    EXPECT_TRUE(action.isConsumed());
    EXPECT_EQ(action.lifecycle(), ToolLifecycle::Cancelled);
}

// mergeEndAction：主 action lifecycle=Running 时 end 不被调用，工具保持 active。
TEST(ToolControllerEndAction, RunningLifecycleKeepsToolActive) {
    ToolController controller;
    std::optional<ToolFinishReason> endReason;
    auto tool = std::make_unique<FakeTool>("tool", endReason);
    tool->handle_action = EditorAction::ignored();  // Running，不触发 end
    controller.start(std::move(tool));

    EditorAction action = controller.handleInput(makeInput(
            InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::Left)));

    EXPECT_FALSE(endReason.has_value());  // end 未被调用
    EXPECT_TRUE(controller.hasActiveTool());
    EXPECT_EQ(action.lifecycle(), ToolLifecycle::Running);
}
