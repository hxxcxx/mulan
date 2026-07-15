/**
 * @file operator_routing_tests.cpp
 * @brief 验证 ViewContext 的 Operator 延迟回收与强类型输入路由。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/interaction/camera_manipulator.h>
#include <mulan/view/core/view_context.h>

#include <gtest/gtest.h>

#include <memory>

namespace mulan::view {
namespace {

class CancelFinishingOperator final : public engine::Operator {
public:
    explicit CancelFinishingOperator(bool& destroyed) : destroyed_(destroyed) {}
    ~CancelFinishingOperator() override { destroyed_ = true; }

    bool onCancel() override {
        finish(false);
        return true;
    }

private:
    bool& destroyed_;
};

class PassiveOperator final : public engine::Operator {
public:
    explicit PassiveOperator(size_t& destructionCount) : destruction_count_(destructionCount) {}
    ~PassiveOperator() override { ++destruction_count_; }

private:
    size_t& destruction_count_;
};

TEST(OperatorRoutingTests, CancelledOperatorIsDestroyedOnlyAfterDispatchReturns) {
    ViewContext view;
    bool destroyed = false;
    view.pushOperator(std::make_unique<CancelFinishingOperator>(destroyed));

    const engine::InputOutcome outcome = view.dispatchInput(engine::InputEvent::focusLost());

    EXPECT_EQ(outcome.disposition, engine::InputDisposition::Cancelled);
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(view.activeOperator(), view.defaultOperator());
}

TEST(OperatorRoutingTests, RemoveOperatorTargetsOnlyTheRequestedStackEntry) {
    ViewContext view;
    size_t firstDestroyed = 0;
    size_t secondDestroyed = 0;

    auto first = std::make_unique<PassiveOperator>(firstDestroyed);
    engine::Operator* firstPtr = first.get();
    view.pushOperator(std::move(first));

    auto second = std::make_unique<PassiveOperator>(secondDestroyed);
    engine::Operator* secondPtr = second.get();
    view.pushOperator(std::move(second));

    ASSERT_EQ(view.activeOperator(), secondPtr);
    EXPECT_TRUE(view.removeOperator(firstPtr));
    EXPECT_EQ(firstDestroyed, 1u);
    EXPECT_EQ(secondDestroyed, 0u);
    EXPECT_EQ(view.activeOperator(), secondPtr);
    EXPECT_FALSE(view.removeOperator(firstPtr));

    EXPECT_TRUE(view.removeOperator(secondPtr));
    EXPECT_EQ(secondDestroyed, 1u);
    EXPECT_EQ(view.activeOperator(), view.defaultOperator());
}

TEST(OperatorRoutingTests, CameraManipulatorReportsNavigationDisposition) {
    engine::CameraManipulator manipulator;
    engine::Camera camera;
    manipulator.setState(engine::Operator::State::Active);

    engine::InputEvent wheel;
    wheel.type = engine::InputEvent::Type::Wheel;
    wheel.wheelDelta = 1;

    const engine::InputOutcome outcome = manipulator.dispatchEvent(wheel, camera);
    EXPECT_EQ(outcome.disposition, engine::InputDisposition::ViewNavigation);
}

}  // namespace
}  // namespace mulan::view
