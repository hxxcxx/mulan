/**
 * @file draft.h
 * @brief 交互 operator 使用的轻量绘制中几何草稿。
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include <mulan/math/math.h>

#include <optional>

namespace mulan::engine {

struct LineDraft {
    std::optional<math::Point3> start;
    std::optional<math::Point3> current;

    bool hasStart() const { return start.has_value(); }
    bool complete() const { return start.has_value() && current.has_value(); }

    void begin(const math::Point3& point) {
        start = point;
        current = point;
    }

    void update(const math::Point3& point) {
        if (start) {
            current = point;
        }
    }

    void reset() {
        start.reset();
        current.reset();
    }

    std::optional<math::Segment3> segment() const {
        if (!complete()) {
            return std::nullopt;
        }
        return math::Segment3(*start, *current);
    }
};

}  // namespace mulan::engine
