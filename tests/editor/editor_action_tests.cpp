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
#include <mulan/editor/core/session/editor_session.h>
#include <mulan/editor/document/document_view.h>
#include <mulan/view/core/view_context.h>
#include <mulan/scene/entity_id.h>

#include <memory>
#include <optional>
#include <vector>

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
    FakeTool(std::string_view id, std::optional<ToolFinishReason>& endReasonSink,
             std::vector<InputEvent>* inputSink = nullptr)
        : id_(id), endReasonSink_(endReasonSink), inputSink_(inputSink) {}

    std::string_view id() const override { return id_; }

    EditorAction begin() override { return EditorAction::ignored(); }

    EditorAction handleInput(const EditorInput& input) override {
        if (inputSink_) {
            inputSink_->push_back(input.event);
        }
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
    std::vector<InputEvent>* inputSink_ = nullptr;
};

/// 不处理输入的占位 Operator，用于验证精确所有权不会误删其他模态交互。
class PassiveOperator final : public Operator {};

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

    EditorAction action =
            controller.handleInput(makeInput(InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::Left)));

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

    controller.handleInput(makeInput(InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::Left)));

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

    EditorAction action =
            controller.handleInput(makeInput(InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::Left)));

    EXPECT_FALSE(endReason.has_value());  // end 未被调用
    EXPECT_TRUE(controller.hasActiveTool());
    EXPECT_EQ(action.lifecycle(), ToolLifecycle::Running);
}

// ============================================================
// Operator / EditorSession 交互边界回归
// ============================================================

TEST(OperatorInputOutcome, CameraDispatchHasStrongNavigationDisposition) {
    Camera camera(CameraMode::Trackball);
    CameraManipulator manipulator;
    manipulator.setState(Operator::State::Active);
    manipulator.onActivate(camera);

    const InputOutcome outcome =
            manipulator.dispatchEvent(InputEvent::mousePress(20, 30, MouseButton::Middle, MouseButton::Middle), camera);

    EXPECT_TRUE(outcome.handled());
    EXPECT_EQ(outcome.disposition, InputDisposition::ViewNavigation);
    manipulator.onDeactivate(camera);
}

TEST(ViewContextOperatorOwnership, RemoveOperatorOnlyRemovesRequestedInstance) {
    mulan::view::ViewContext view;
    auto first = std::make_unique<PassiveOperator>();
    Operator* firstPtr = first.get();
    auto second = std::make_unique<PassiveOperator>();
    Operator* secondPtr = second.get();
    view.pushOperator(std::move(first));
    view.pushOperator(std::move(second));

    EXPECT_TRUE(view.removeOperator(firstPtr));
    EXPECT_EQ(view.activeOperator(), secondPtr);
    EXPECT_FALSE(view.removeOperator(firstPtr));
    view.popOperator();
    EXPECT_EQ(view.activeOperator(), view.defaultOperator());
}

TEST(EditorToolOperatorLifecycle, ReplacementKeepsUnrelatedOperatorAndHasNoGhost) {
    mulan::view::ViewContext view;
    EditorSession session;
    session.bind(nullptr, &view, nullptr);  // 纯交互测试，不初始化 GPU / 文档绑定

    std::optional<ToolFinishReason> firstEndReason;
    session.startTool(std::make_unique<FakeTool>("first", firstEndReason));

    auto unrelated = std::make_unique<PassiveOperator>();
    Operator* unrelatedPtr = unrelated.get();
    view.pushOperator(std::move(unrelated));

    std::optional<ToolFinishReason> secondEndReason;
    session.startTool(std::make_unique<FakeTool>("second", secondEndReason));
    ASSERT_TRUE(firstEndReason.has_value());
    EXPECT_EQ(*firstEndReason, ToolFinishReason::Replaced);

    session.cancelActiveTool();
    EXPECT_EQ(view.activeOperator(), unrelatedPtr);
    view.popOperator();
    session.unbind();
}

TEST(EditorToolOperatorLifecycle, CameraDelegateIsActiveAndRightClickBelongsToTool) {
    mulan::view::ViewContext view;
    EditorSession session;
    session.bind(nullptr, &view, nullptr);

    std::optional<ToolFinishReason> endReason;
    std::vector<InputEvent> inputs;
    session.startTool(std::make_unique<FakeTool>("tool", endReason, &inputs));

    const InputOutcome navigation =
            view.dispatchInput(InputEvent::mousePress(10, 10, MouseButton::Middle, MouseButton::Middle));
    EXPECT_EQ(navigation.disposition, InputDisposition::ViewNavigation);
    EXPECT_TRUE(inputs.empty());

    const InputOutcome toolInput =
            view.dispatchInput(InputEvent::mousePress(10, 10, MouseButton::Right, MouseButton::Right));
    EXPECT_EQ(toolInput.disposition, InputDisposition::ModalInteraction);
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_TRUE(inputs.front().isRightPress());

    // cancel 在 EditorToolOperator 内只结束工具；ViewContext 在调用返回后再安全析构它。
    const InputOutcome cancelled = view.dispatchInput(InputEvent::focusLost());
    EXPECT_EQ(cancelled.disposition, InputDisposition::Cancelled);
    EXPECT_FALSE(session.hasActiveTool());
    EXPECT_EQ(view.activeOperator(), view.defaultOperator());
    session.unbind();
}

TEST(DocumentViewInputBoundary, ActiveToolReleaseNeverFallsBackToSelection) {
    DocumentView documentView;
    EditorSession* session = documentView.commandHost().editorSession();
    ASSERT_NE(session, nullptr);
    session->bind(nullptr, &documentView.viewContext(), nullptr);

    std::optional<ToolFinishReason> endReason;
    session->startTool(std::make_unique<FakeTool>("tool", endReason));
    documentView.handleInput(InputEvent::mousePress(40, 50, MouseButton::Left, MouseButton::Left));
    const DocumentInputOutcome release =
            documentView.handleInput(InputEvent::mouseRelease(40, 50, MouseButton::Left, MouseButton::None));

    EXPECT_EQ(release.disposition, DocumentInputDisposition::EditorTool);
    EXPECT_NE(release.disposition, DocumentInputDisposition::Selection);
    documentView.cancelActiveEditorTool();
    session->unbind();
}

TEST(DocumentViewInputBoundary, CancelClearsPendingClickTracking) {
    DocumentView documentView;
    documentView.handleInput(InputEvent::mousePress(40, 50, MouseButton::Left, MouseButton::Left));
    const DocumentInputOutcome cancelled = documentView.cancelInteraction();
    const DocumentInputOutcome release =
            documentView.handleInput(InputEvent::mouseRelease(40, 50, MouseButton::Left, MouseButton::None));

    EXPECT_EQ(cancelled.disposition, DocumentInputDisposition::Cancelled);
    EXPECT_NE(release.disposition, DocumentInputDisposition::Selection);
}
