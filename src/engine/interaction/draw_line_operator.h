/**
 * @file draw_line_operator.h
 * @brief DrawLineOperator 两点式线段绘制工具
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 该工具只处理交互状态，不直接依赖 Document。宿主通过回调提供屏幕坐标到世界坐标的
 * 转换、动态预览和最终提交，从而保持 engine 层与应用文档层解耦。
 */
#pragma once

#include "camera_manipulator.h"
#include "operator.h"
#include "work_plane.h"

#include <mulan/math/math.h>

#include <functional>
#include <optional>

namespace mulan::engine {

class DrawLineOperator : public Operator {
public:
    using PointResolver = std::function<std::optional<math::Point3>(const InputEvent&, const Camera&)>;
    using PreviewCallback = std::function<void(const math::Point3&, const math::Point3&)>;
    using CommitCallback = std::function<void(const math::Point3&, const math::Point3&)>;
    using ClearPreviewCallback = std::function<void()>;

    explicit DrawLineOperator(PointResolver resolver = {});

    void setPointResolver(PointResolver resolver) { point_resolver_ = std::move(resolver); }
    void setPreviewCallback(PreviewCallback callback) { preview_callback_ = std::move(callback); }
    void setCommitCallback(CommitCallback callback) { commit_callback_ = std::move(callback); }
    void setClearPreviewCallback(ClearPreviewCallback callback) { clear_preview_callback_ = std::move(callback); }

    void onActivate(Camera& cam) override;
    void onDeactivate(Camera& cam) override;

    bool onMousePress(const InputEvent& e, Camera& cam) override;
    bool onMouseMove(const InputEvent& e, Camera& cam) override;
    bool onKeyPress(const InputEvent& e, Camera& cam) override;

protected:
    bool isCameraEvent(const InputEvent& e) const override;
    bool handleCameraEvent(const InputEvent& e, Camera& cam) override;

private:
    std::optional<math::Point3> resolvePoint(const InputEvent& e, const Camera& cam) const;
    void updatePreview(const math::Point3& current);
    void clearPreview();
    void cancel();

    PointResolver point_resolver_;
    PreviewCallback preview_callback_;
    CommitCallback commit_callback_;
    ClearPreviewCallback clear_preview_callback_;

    CameraManipulator camera_op_;
    WorkPlane work_plane_ = WorkPlane::worldXY();
    std::optional<math::Point3> first_point_;
};

}  // namespace mulan::engine
