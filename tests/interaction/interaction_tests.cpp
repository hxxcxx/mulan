/**
 * @file interaction_tests.cpp
 * @brief 交互层纯逻辑测试：InputEvent 谓词、CameraManipulator 按钮、Operator 状态机。
 *
 * 不依赖 GPU / Qt / 渲染后端，完全离屏运行。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <gtest/gtest.h>

#include <mulan/interaction/input_event.h>
#include <mulan/interaction/camera_manipulator.h>
#include <mulan/render/camera/camera.h>

#include <cmath>
#include <limits>

using namespace mulan::engine;

namespace {
// 状态机测试不需要真实相机操作，提供一个共享占位实例。
Camera& sharedCam() {
    static Camera c{ CameraMode::Trackball };
    return c;
}

mulan::math::Point3 cursorPointOnTargetPlane(const Camera& camera, double x, double y) {
    const auto ray = camera.screenRay(x, y);
    const auto normal = camera.forward();
    const mulan::math::Point3 planePoint(camera.target().x, camera.target().y, camera.target().z);
    const double denominator = ray.direction.dot(normal);
    const double distance = (planePoint - ray.origin).dot(normal) / denominator;
    return ray.pointAt(distance);
}
}  // namespace

// ============================================================
// InputEvent 谓词（从 5 个文件去重集中到成员方法）
// ============================================================

TEST(InputEventPredicateTest, LeftPress) {
    InputEvent e = InputEvent::mousePress(10, 20, MouseButton::Left, MouseButton::None);
    EXPECT_TRUE(e.isLeftPress());
    EXPECT_FALSE(e.isLeftRelease());
    EXPECT_FALSE(e.isRightPress());
    EXPECT_FALSE(e.isMouseMove());
}

TEST(InputEventPredicateTest, LeftRelease) {
    InputEvent e = InputEvent::mouseRelease(10, 20, MouseButton::Left, MouseButton::None);
    EXPECT_TRUE(e.isLeftRelease());
    EXPECT_FALSE(e.isLeftPress());
}

TEST(InputEventPredicateTest, RightPress) {
    InputEvent e = InputEvent::mousePress(10, 20, MouseButton::Right, MouseButton::None);
    EXPECT_TRUE(e.isRightPress());
    EXPECT_FALSE(e.isLeftPress());
}

TEST(InputEventPredicateTest, MouseMove) {
    InputEvent e = InputEvent::mouseMove(5, 5, MouseButton::None);
    EXPECT_TRUE(e.isMouseMove());
    EXPECT_FALSE(e.isLeftPress());
}

TEST(InputEventPredicateTest, MoveButtonIsNone) {
    // move 的 button 恒为 None（工厂保证），这是 isCameraEvent 必须查 buttons 的根因。
    InputEvent e = InputEvent::mouseMove(5, 5, MouseButton::Left);
    EXPECT_EQ(e.button, MouseButton::None);
    EXPECT_TRUE(e.buttons & MouseButton::Left);
}

TEST(InputEventPredicateTest, CancelEvents) {
    EXPECT_TRUE(InputEvent::pointerCancel().isCancelEvent());
    EXPECT_TRUE(InputEvent::focusLost().isCancelEvent());
    EXPECT_FALSE(InputEvent::mousePress(0, 0, MouseButton::Left, MouseButton::None).isCancelEvent());
}

// ============================================================
// CameraManipulator —— 按钮匹配与多按钮修复
// ============================================================

class CameraManipulatorTest : public ::testing::Test {
protected:
    Camera cam{ CameraMode::Trackball };
    CameraManipulator manip;

    void SetUp() override { manip.setState(Operator::State::Active); }
};

// 修复 P2：非导航按钮（左键）press 不应抢占。
TEST_F(CameraManipulatorTest, LeftPressNotConsumedByCamera) {
    // 默认 orbitButton=Left，但只有当按钮匹配 orbit/pan 时才抢占。
    // 左键匹配 orbitButton，所以左键应被消费——这是默认映射下的预期。
    InputEvent press = InputEvent::mousePress(100, 100, MouseButton::Left, MouseButton::None);
    EXPECT_TRUE(manip.onMousePress(press, cam));
}

TEST_F(CameraManipulatorTest, MiddlePressConsumed) {
    InputEvent press = InputEvent::mousePress(100, 100, MouseButton::Middle, MouseButton::None);
    EXPECT_TRUE(manip.onMousePress(press, cam));
}

// 修复 P3：release 只结束匹配启动按钮的 drag，多按钮错序不提前终止。
TEST_F(CameraManipulatorTest, ReleaseMatchesStartButton) {
    // 中键启动 drag
    InputEvent midPress = InputEvent::mousePress(100, 100, MouseButton::Middle, MouseButton::Middle);
    ASSERT_TRUE(manip.onMousePress(midPress, cam));

    // 左键 release（错序）不应结束中键 drag
    InputEvent leftRelease = InputEvent::mouseRelease(100, 100, MouseButton::Left, MouseButton::Middle);
    EXPECT_FALSE(manip.onMouseRelease(leftRelease, cam));

    // 中键 release 才结束
    InputEvent midRelease = InputEvent::mouseRelease(100, 100, MouseButton::Middle, MouseButton::None);
    EXPECT_TRUE(manip.onMouseRelease(midRelease, cam));
}

TEST_F(CameraManipulatorTest, ReleaseWithoutPressIgnored) {
    InputEvent release = InputEvent::mouseRelease(100, 100, MouseButton::Middle, MouseButton::None);
    EXPECT_FALSE(manip.onMouseRelease(release, cam));
}

TEST_F(CameraManipulatorTest, MoveRequiresDrag) {
    InputEvent move = InputEvent::mouseMove(200, 200, MouseButton::None);
    EXPECT_FALSE(manip.onMouseMove(move, cam));  // 未 press，move 被忽略
}

TEST_F(CameraManipulatorTest, WheelZooms) {
    InputEvent wheel = InputEvent::wheel(100, 100, 1.0f);
    EXPECT_TRUE(manip.onWheel(wheel, cam));
}

TEST(CameraZoomTest, OrthographicZoomKeepsCursorWorldPointFixed) {
    Camera camera{ CameraMode::Trackball };
    camera.setViewport(800, 600);
    camera.setOrthographic(true);
    camera.setOrthoSize(5.0);
    constexpr double cursorX = 620.0;
    constexpr double cursorY = 170.0;
    const auto before = cursorPointOnTargetPlane(camera, cursorX, cursorY);

    camera.zoomAt(-2.0, cursorX, cursorY);

    const auto after = cursorPointOnTargetPlane(camera, cursorX, cursorY);
    EXPECT_NEAR(after.x, before.x, 1.0e-9);
    EXPECT_NEAR(after.y, before.y, 1.0e-9);
    EXPECT_NEAR(after.z, before.z, 1.0e-9);
}

TEST(CameraZoomTest, PerspectiveZoomKeepsCursorTargetPlanePointFixed) {
    Camera camera{ CameraMode::Trackball };
    camera.setViewport(800, 600);
    camera.setOrthographic(false);
    camera.setDistance(10.0);
    constexpr double cursorX = 610.0;
    constexpr double cursorY = 180.0;
    const auto before = cursorPointOnTargetPlane(camera, cursorX, cursorY);

    camera.zoomAt(-2.0, cursorX, cursorY);

    const auto after = cursorPointOnTargetPlane(camera, cursorX, cursorY);
    EXPECT_NEAR(after.x, before.x, 1.0e-9);
    EXPECT_NEAR(after.y, before.y, 1.0e-9);
    EXPECT_NEAR(after.z, before.z, 1.0e-9);
}

// cancel 幂等：重复调用不崩溃，清理 dragging_ 状态。
TEST_F(CameraManipulatorTest, CancelIsIdempotent) {
    InputEvent press = InputEvent::mousePress(100, 100, MouseButton::Middle, MouseButton::Middle);
    manip.onMousePress(press, cam);

    manip.cancel(CancelReason::FocusLost);
    manip.cancel(CancelReason::System);  // 重复调用

    // cancel 后 move 应被忽略（dragging_ 已清）
    InputEvent move = InputEvent::mouseMove(200, 200, MouseButton::Middle);
    EXPECT_FALSE(manip.onMouseMove(move, cam));
}

TEST(CameraClipPlanesTest, OffAxisSmallSphereUsesForwardDepth) {
    Camera camera{ CameraMode::Trackball };
    camera.setDistance(10.0);
    camera.setClipPlanes(0.1, 1000.0);

    // 小图元位于屏幕边缘时，欧氏距离明显大于沿视线的真实深度。
    // near/far 必须按前向投影深度计算，否则 near 会越过整个图元。
    const mulan::math::Sphere3 sphere{ mulan::math::Point3(5.0, 0.0, 0.0), 0.01 };
    const double depth = (sphere.center.asVec() - camera.eyePosition()).dot(camera.forward());

    camera.fitClipPlanesToSphere(sphere);

    EXPECT_LT(camera.nearPlane(), depth - sphere.radius);
    EXPECT_GT(camera.farPlane(), depth + sphere.radius);
    EXPECT_TRUE(std::isfinite(camera.nearPlane()));
    EXPECT_TRUE(std::isfinite(camera.farPlane()));
}

TEST(CameraClipPlanesTest, InvalidCandidatesKeepLastGoodRange) {
    Camera camera{ CameraMode::Trackball };
    camera.setClipPlanes(0.25, 250.0);
    const double initialNear = camera.nearPlane();
    const double initialFar = camera.farPlane();
    const double infinity = std::numeric_limits<double>::infinity();

    camera.setClipPlanes(infinity, infinity);
    camera.fitClipPlanesToSphere(mulan::math::Sphere3{ mulan::math::Point3(infinity, 0.0, 0.0), 1.0 });
    camera.fitClipPlanesToSphere(mulan::math::Sphere3{ mulan::math::Point3::origin(), infinity });

    EXPECT_DOUBLE_EQ(camera.nearPlane(), initialNear);
    EXPECT_DOUBLE_EQ(camera.farPlane(), initialFar);
}

TEST(CameraClipPlanesTest, InteractiveFitExpandsButNeverShrinks) {
    Camera camera{ CameraMode::Trackball };
    camera.setDistance(10.0);
    camera.setClipPlanes(0.1, 1000.0);
    const mulan::math::Sphere3 compact{ mulan::math::Point3::origin(), 1.0 };

    camera.fitClipPlanesToSphere(compact, 1.2, ClipPlaneFitMode::ExpandOnly);
    EXPECT_DOUBLE_EQ(camera.nearPlane(), 0.1);
    EXPECT_DOUBLE_EQ(camera.farPlane(), 1000.0);

    camera.fitClipPlanesToSphere(compact, 1.2, ClipPlaneFitMode::Tight);
    EXPECT_GT(camera.nearPlane(), 0.1);
    EXPECT_LT(camera.farPlane(), 1000.0);

    camera.setClipPlanes(9.5, 10.5);
    camera.fitClipPlanesToSphere(compact, 1.2, ClipPlaneFitMode::ExpandOnly);
    EXPECT_LT(camera.nearPlane(), 9.0);
    EXPECT_GT(camera.farPlane(), 11.0);
}

TEST(CameraClipPlanesTest, OrthographicFitMovesEyeBehindLargeBoundsWithoutChangingScale) {
    Camera camera{ CameraMode::Trackball };
    camera.setOrthographic(true);
    camera.setOrthoSize(150.0);
    camera.setDistance(10.0);
    const double originalOrthoSize = camera.orthoSize();
    const mulan::math::Sphere3 sphere{ mulan::math::Point3::origin(), 100.0 };

    camera.fitClipPlanesToSphere(sphere, 1.2, ClipPlaneFitMode::ExpandOnly);

    const double centerDepth = (sphere.center.asVec() - camera.eyePosition()).dot(camera.forward());
    EXPECT_GT(centerDepth - sphere.radius, camera.nearPlane());
    EXPECT_GT(camera.distance(), sphere.radius);
    EXPECT_DOUBLE_EQ(camera.orthoSize(), originalOrthoSize);
}

TEST(CameraDepthRevisionTest, OnlyDepthRelevantChangesAdvanceRevision) {
    Camera camera{ CameraMode::Trackball };
    const uint64_t initial = camera.depthRevision();

    camera.pan(10.0, 5.0);
    camera.setOrthoSize(2.0);
    camera.zoom(-1.0);
    EXPECT_EQ(camera.depthRevision(), initial);

    camera.setTarget({ 1.0, 0.0, 0.0 });
    const uint64_t afterTarget = camera.depthRevision();
    EXPECT_GT(afterTarget, initial);

    camera.setOrthographic(false);
    camera.zoom(-1.0);
    EXPECT_GT(camera.depthRevision(), afterTarget);
}

TEST(CameraFitTest, NarrowViewportUsesHorizontalConstraint) {
    const mulan::math::Sphere3 sphere{ mulan::math::Point3::origin(), 2.0 };

    Camera orthographic{ CameraMode::Trackball };
    orthographic.setViewport(400, 800);
    orthographic.fitToSphere(sphere, 1.2);
    EXPECT_DOUBLE_EQ(orthographic.orthoSize(), 4.8);

    Camera widePerspective{ CameraMode::Trackball };
    widePerspective.setOrthographic(false);
    widePerspective.setViewport(800, 400);
    widePerspective.fitToSphere(sphere, 1.2);

    Camera narrowPerspective{ CameraMode::Trackball };
    narrowPerspective.setOrthographic(false);
    narrowPerspective.setViewport(400, 800);
    narrowPerspective.fitToSphere(sphere, 1.2);
    EXPECT_GT(narrowPerspective.distance(), widePerspective.distance());
}

TEST(CameraInvariantTest, InvalidPublicParametersKeepLastGoodState) {
    Camera camera{ CameraMode::Trackball };
    const double infinity = std::numeric_limits<double>::infinity();
    const auto target = camera.target();
    const double distance = camera.distance();
    const double orthoSize = camera.orthoSize();
    const double fov = camera.fieldOfView();

    camera.setViewport(0, -1);
    camera.setTarget({ infinity, 0.0, 0.0 });
    camera.setDistance(-1.0);
    camera.setOrthoSize(0.0);
    camera.setFieldOfView(infinity);
    camera.setRotation({ infinity, 0.0, 0.0, 0.0 });

    EXPECT_EQ(camera.width(), 1);
    EXPECT_EQ(camera.height(), 1);
    EXPECT_EQ(camera.target(), target);
    EXPECT_DOUBLE_EQ(camera.distance(), distance);
    EXPECT_DOUBLE_EQ(camera.orthoSize(), orthoSize);
    EXPECT_DOUBLE_EQ(camera.fieldOfView(), fov);
}

// ============================================================
// Operator 状态机
// ============================================================

namespace {
// 测试专用 Operator，公开 protected finish 以便直接验证状态转换。
class TestOperator : public Operator {
public:
    using Operator::finish;  // 公开 finish
    bool onMousePress(const InputEvent&, Camera&) override { return true; }
};
}  // namespace

TEST(OperatorStateMachineTest, InitialStateInactive) {
    TestOperator m;
    EXPECT_EQ(m.state(), Operator::State::Inactive);
    EXPECT_FALSE(m.isActive());
    EXPECT_FALSE(m.isFinished());
}

TEST(OperatorStateMachineTest, FinishIsIdempotent) {
    TestOperator m;
    m.finish(true);
    EXPECT_EQ(m.state(), Operator::State::Finished);
    m.finish(false);  // 重复 finish 不改变状态
    EXPECT_EQ(m.state(), Operator::State::Finished);
    EXPECT_TRUE(m.isCompleted());
}

TEST(OperatorStateMachineTest, CancelSetsCancelledState) {
    TestOperator m;
    m.finish(false);
    EXPECT_EQ(m.state(), Operator::State::Cancelled);
    EXPECT_TRUE(m.isFinished());
    EXPECT_FALSE(m.isCompleted());
}

// handleEvent 对 cancel 事件优先走 cancel 路径
TEST(OperatorStateMachineTest, CancelEventTriggersCancel) {
    CameraManipulator m;
    m.setState(Operator::State::Active);
    // 先 press 进入 dragging
    InputEvent press = InputEvent::mousePress(50, 50, MouseButton::Middle, MouseButton::Middle);
    m.onMousePress(press, sharedCam());

    InputEvent cancel = InputEvent::focusLost();
    m.handleEvent(cancel, sharedCam());
    // cancel 清理了 dragging，后续 move 不再响应
    InputEvent move = InputEvent::mouseMove(60, 60, MouseButton::Middle);
    EXPECT_FALSE(m.handleEvent(move, sharedCam()));
}

TEST(OperatorStateMachineTest, InactiveDoesNotHandle) {
    CameraManipulator m;
    // 未 Active，handleEvent 应返回 false
    InputEvent press = InputEvent::mousePress(50, 50, MouseButton::Left, MouseButton::None);
    EXPECT_FALSE(m.handleEvent(press, sharedCam()));
}
