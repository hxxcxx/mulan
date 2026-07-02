#pragma once

#include <cstdint>

namespace mulan::scene {

enum class EntityDirty : uint64_t {
    None        = 0,
    Created     = 1ull << 0,
    Destroyed   = 1ull << 1,
    Name        = 1ull << 2,
    Transform   = 1ull << 3,
    Hierarchy   = 1ull << 4,
    Geometry    = 1ull << 5,
    RenderState = 1ull << 6,
    Material    = 1ull << 7,
    Selection   = 1ull << 8,
    Bounds      = 1ull << 9,

    RenderRelated = (1ull << 3) | (1ull << 5) | (1ull << 6) | (1ull << 7) | (1ull << 8),
    BoundsRelated = (1ull << 3) | (1ull << 5),
};

inline EntityDirty operator|(EntityDirty a, EntityDirty b) {
    return static_cast<EntityDirty>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

inline EntityDirty operator&(EntityDirty a, EntityDirty b) {
    return static_cast<EntityDirty>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

inline EntityDirty& operator|=(EntityDirty& a, EntityDirty b) {
    a = a | b;
    return a;
}

inline uint64_t dirtyValue(EntityDirty dirty) {
    return static_cast<uint64_t>(dirty);
}

inline bool hasDirty(uint64_t flags, EntityDirty dirty) {
    return (flags & dirtyValue(dirty)) != 0;
}

inline bool hasAnyDirty(uint64_t flags, EntityDirty mask) {
    return (flags & dirtyValue(mask)) != 0;
}

} // namespace mulan::scene
