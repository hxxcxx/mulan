/**
 * @file transform_edit_context.h
 * @brief TransformEditContext 描述一次变换编辑中的目标集合与初始状态。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "core/operation/document_operation.h"
#include "core/operation/selection_target.h"

#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <cstdint>
#include <span>
#include <vector>

namespace mulan::io {
class Document;
}

namespace mulan::app {

enum class TransformEditMode : uint8_t {
    Translate,
    Rotate,
    Scale,
};

enum class TransformEditCommitMode : uint8_t {
    Move,
    Copy,
};

enum class TransformEditSubjectKind : uint8_t {
    Entity,
    SubObject,
};

struct TransformEditSubject {
    SelectionTarget target;
    TransformEditSubjectKind kind = TransformEditSubjectKind::Entity;
    math::Mat4 initialWorldTransform{ 1.0 };
    bool hasInitialWorldTransform = false;

    bool valid() const { return target.valid(); }
    bool wholeEntity() const { return kind == TransformEditSubjectKind::Entity && target.wholeEntity(); }
    bool subObject() const { return kind == TransformEditSubjectKind::SubObject; }
};

class TransformEditContext {
public:
    static TransformEditContext fromSelection(const io::Document& document, std::span<const SelectionTarget> selection);
    static TransformEditContext fromTarget(const io::Document& document, const SelectionTarget& target);

    bool empty() const { return subjects_.empty(); }
    std::span<const TransformEditSubject> subjects() const { return subjects_; }

    void setAnchorWorld(math::Point3 anchor) {
        anchor_world_ = anchor;
        has_anchor_world_ = true;
    }
    bool hasAnchorWorld() const { return has_anchor_world_; }
    const math::Point3& anchorWorld() const { return anchor_world_; }

    std::vector<EntityTransformUpdate> entityUpdates(const math::Mat4& worldDelta) const;

private:
    void addSubject(const io::Document& document, const SelectionTarget& target);

    std::vector<TransformEditSubject> subjects_;
    math::Point3 anchor_world_;
    bool has_anchor_world_ = false;
};

}  // namespace mulan::app
