/**
 * @file drag_edit_session.h
 * @brief DragEditSession 统一描述编辑拖动过程中的目标与坐标采样。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "selection_target.h"

#include <mulan/math/math.h>

#include <cstdint>
#include <optional>

namespace mulan::app {

enum class DragEditSubjectKind : uint8_t {
    Entity,
    SubObject,
    Grip,
    Gizmo,
};

struct DragEditDescriptor {
    SelectionTarget target;
    DragEditSubjectKind subjectKind = DragEditSubjectKind::SubObject;
    math::Point3 startWorld;
    math::Mat4 localToWorld{ 1.0 };
    math::Mat4 worldToLocal{ 1.0 };
};

struct DragEditSample {
    SelectionTarget target;
    DragEditSubjectKind subjectKind = DragEditSubjectKind::SubObject;
    math::Point3 startWorld;
    math::Point3 currentWorld;
    math::Vec3 deltaWorld;
    math::Point3 startLocal;
    math::Point3 currentLocal;
    math::Vec3 deltaLocal;
};

class DragEditSession {
public:
    explicit DragEditSession(DragEditDescriptor descriptor);

    const DragEditDescriptor& descriptor() const { return descriptor_; }
    const std::optional<DragEditSample>& lastSample() const { return last_sample_; }

    DragEditSample sampleAt(const math::Point3& currentWorld) const;
    DragEditSample update(const math::Point3& currentWorld);
    void clearPreview() { last_sample_.reset(); }

private:
    DragEditDescriptor descriptor_;
    math::Point3 start_local_;
    std::optional<DragEditSample> last_sample_;
};

}  // namespace mulan::app
