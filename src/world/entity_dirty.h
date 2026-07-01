/**
 * @file entity_dirty.h
 * @brief Entity 脏标记位掩码 — System 按掩码增量拉取变更
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include <cstdint>

namespace mulan::world {

enum class EntityDirty : uint64_t {
    None        = 0,

    // 生命周期
    Created     = 1ull << 0,
    Destroyed   = 1ull << 1,

    // 属性
    Transform   = 1ull << 2,
    Parent      = 1ull << 3,
    Geometry    = 1ull << 4,
    Visibility  = 1ull << 5,
    Material    = 1ull << 6,
    Selection   = 1ull << 8,
    Name        = 1ull << 9,

    // 常用组合
    RenderRelated = Transform | Geometry | Visibility | Material,
    BoundsRelated = Transform | Geometry,
};

inline EntityDirty operator|(EntityDirty a, EntityDirty b) {
    return static_cast<EntityDirty>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

inline EntityDirty operator&(EntityDirty a, EntityDirty b) {
    return static_cast<EntityDirty>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

inline uint64_t dirtyValue(EntityDirty d) {
    return static_cast<uint64_t>(d);
}

inline bool hasDirty(uint64_t flags, EntityDirty test) {
    return (flags & static_cast<uint64_t>(test)) != 0;
}

inline bool hasAnyDirty(uint64_t flags, EntityDirty mask) {
    return (flags & static_cast<uint64_t>(mask)) != 0;
}

} // namespace mulan::world
