/**
 * @file render_packet.h
 * @brief 定义 RenderCompiler 子系统之间共享的只读渲染 Packet。
 * @author hxxcxx
 * @date 2026-07-21
 *
 * 本文件属于 render backend 内部实现，不是对外渲染接口。
 */

#pragma once

#include "../../mesh_draw_command.h"
#include "../../frontend/render_object.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mulan::engine::detail {

enum class DrawableStatus : uint8_t {
    Ready,
    MissingGeometryRecord,
    EmptyGeometry,
    MissingGpuGeometry,
    MissingGpuTexture,
    RejectedContract,
    MaterialRegistrationFailure,
};

struct CachedRenderDrawable {
    RenderBucket bucket = RenderBucket::Surface;
    size_t sourceDrawableIndex = 0;
    DrawableStatus geometryStatus = DrawableStatus::Ready;
    DrawableStatus materialStatus = DrawableStatus::Ready;
    DrawableStatus textureStatus = DrawableStatus::Ready;
    size_t missingTextureCount = 0;
    MeshDrawCommand baseCommand;
    PipelineState* highlightPipeline = nullptr;
};

struct RenderPacket {
    RenderObjectId id;
    PickId pickId;
    math::AABB3 worldBounds;
    bool visible = false;
    std::vector<CachedRenderDrawable> drawables;
};

/// 可见性索引只消费对象身份和包围盒，不依赖 Packet 中的 GPU/材质内容。
struct VisibilityItem {
    RenderObjectId id;
    math::AABB3 bounds;
};

}  // namespace mulan::engine::detail
