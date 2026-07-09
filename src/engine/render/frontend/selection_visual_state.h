/**
 * @file selection_visual_state.h
 * @brief SelectionVisualState 描述渲染层可消费的 hover / selected 高亮目标。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

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
    SolidFace,
    ControlPoint,
    Grip,
};

struct SelectionVisualTarget {
    uint32_t pickId = 0;
    bool hasPickId = false;
    SelectionVisualRole role = SelectionVisualRole::Selected;
    SelectionVisualDomain domain = SelectionVisualDomain::Entity;
    uint32_t sourceDrawableIndex = 0;
    bool hasSourceDrawableIndex = false;
    uint32_t primitiveIndex = 0;
    bool hasPrimitiveIndex = false;
    uint32_t componentIndex = 0;
    bool hasComponentIndex = false;

    bool valid() const { return hasPickId; }
    bool wholeEntity() const { return domain == SelectionVisualDomain::Entity; }
};

class SelectionVisualState {
public:
    bool active() const { return active_; }
    void setActive(bool active) { active_ = active; }

    bool empty() const { return targets_.empty(); }
    std::span<const SelectionVisualTarget> targets() const { return targets_; }

    void add(SelectionVisualTarget target) {
        if (!target.valid()) {
            return;
        }
        active_ = true;
        targets_.push_back(target);
    }

    void clearTargets() { targets_.clear(); }

    void clear() {
        active_ = false;
        targets_.clear();
    }

private:
    bool active_ = false;
    std::vector<SelectionVisualTarget> targets_;
};

}  // namespace mulan::engine
