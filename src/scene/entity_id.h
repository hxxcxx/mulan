/**
 * @file entity_id.h
 * @brief EntityId —— 带 generation 校验的场景实体标识
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include <cstdint>
#include <functional>

namespace mulan::scene {

using EntityIdValue = uint64_t;

struct EntityId {
    EntityIdValue value = 0;

    static constexpr int INDEX_BITS = 32;
    static constexpr uint32_t INDEX_MASK = ~uint32_t(0);

    static constexpr EntityId invalid() { return {}; }

    constexpr bool valid() const { return value != 0; }
    constexpr explicit operator bool() const { return valid(); }

    constexpr uint32_t index() const { return static_cast<uint32_t>(value & INDEX_MASK); }

    constexpr uint32_t generation() const { return static_cast<uint32_t>(value >> INDEX_BITS); }

    friend constexpr bool operator==(EntityId a, EntityId b) { return a.value == b.value; }

    friend constexpr bool operator!=(EntityId a, EntityId b) { return !(a == b); }
};

}  // namespace mulan::scene

template <>
struct std::hash<mulan::scene::EntityId> {
    size_t operator()(mulan::scene::EntityId id) const noexcept {
        return std::hash<mulan::scene::EntityIdValue>{}(id.value);
    }
};
