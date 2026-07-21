/**
 * @file selection_visual_state.h
 * @brief SelectionVisualState 描述渲染层可消费的 hover / selected 高亮目标。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include "pick_identity.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mulan::engine {

enum class SelectionVisualRole : uint8_t {
    Selected,
    Hovered,
};

enum class SelectionVisualDomain : uint8_t {
    Entity,
    CurveElement,
    CurveSegment,
    CurveVertex,
    MeshFace,
    MeshEdge,
    MeshVertex,
    SurfaceFace,
    SurfaceEdge,
    SurfaceVertex,
    SolidFace,
    SolidEdge,
    SolidVertex,
    ControlPoint,
    Grip,
};

struct SelectionVisualTarget {
    PickId pickId;
    SelectionVisualRole role = SelectionVisualRole::Selected;
    SelectionVisualDomain domain = SelectionVisualDomain::Entity;
    uint32_t sourceDrawableIndex = 0;
    bool hasSourceDrawableIndex = false;
    uint32_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    uint32_t componentIndex = 0;
    bool hasComponentIndex = false;

    bool valid() const { return pickId.valid(); }
    bool wholeEntity() const { return domain == SelectionVisualDomain::Entity; }
};

class SelectionVisualState {
public:
    bool empty() const { return targets_.empty(); }
    std::span<const SelectionVisualTarget> targets() const { return targets_; }

    void add(SelectionVisualTarget target) {
        if (!target.valid()) {
            return;
        }
        targets_.push_back(target);
    }

    void clear() { targets_.clear(); }

private:
    std::vector<SelectionVisualTarget> targets_;
};

}  // namespace mulan::engine
