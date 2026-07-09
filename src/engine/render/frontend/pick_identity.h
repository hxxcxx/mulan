/**
 * @file pick_identity.h
 * @brief PickId 描述渲染拾取链路中可显式判定有效性的对象身份。
 * @author hxxcxx
 * @date 2026-07-09
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

struct PickId {
    uint32_t value = 0;
    bool hasValue = false;

    static constexpr PickId invalid() { return {}; }
    static constexpr PickId fromValue(uint32_t value) { return PickId{ value, true }; }

    constexpr bool valid() const { return hasValue; }
    constexpr explicit operator bool() const { return valid(); }
    constexpr uint32_t valueOr(uint32_t fallback) const { return valid() ? value : fallback; }

    friend constexpr bool operator==(PickId lhs, PickId rhs) {
        return lhs.hasValue == rhs.hasValue && (!lhs.hasValue || lhs.value == rhs.value);
    }

    friend constexpr bool operator!=(PickId lhs, PickId rhs) { return !(lhs == rhs); }
};

}  // namespace mulan::engine
