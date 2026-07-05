/**
 * @file render_handle.h
 * @brief 定义 engine frontend 使用的带 generation 的强类型资源句柄。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <cstdint>
#include <limits>

namespace mulan::engine {

template <typename Tag>
struct RenderHandle {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    constexpr bool isValid() const { return index != std::numeric_limits<uint32_t>::max(); }

    constexpr explicit operator bool() const { return isValid(); }

    friend constexpr bool operator==(RenderHandle lhs, RenderHandle rhs) {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }
};

struct GeometryHandleTag;
struct MaterialHandleTag;
struct TextureHandleTag;
struct RenderObjectIdTag;

using GeometryHandle = RenderHandle<GeometryHandleTag>;
using RenderMaterialHandle = RenderHandle<MaterialHandleTag>;
using RenderTextureHandle = RenderHandle<TextureHandleTag>;
using RenderObjectId = RenderHandle<RenderObjectIdTag>;

}  // namespace mulan::engine
