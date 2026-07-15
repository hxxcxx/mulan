/**
 * @file render_object.h
 * @brief 定义 engine frontend 的渲染对象、材质、纹理和几何描述。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "pick_identity.h"
#include "render_handle.h"
#include "../asset_gpu_key.h"
#include "../material/material.h"

#include <mulan/core/image/image.h>
#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mulan::engine {

enum class RenderBucket : uint8_t {
    Surface,
    Edge,
    OverlaySurface,
    OverlayEdge,
    Gizmo,
    Text,
};

struct RenderTextureDesc {
    RenderResourceKey resourceKey;             ///< 结构化资源身份，用作 GPU 贴图去重键
    std::shared_ptr<const core::Image> image;  ///< 解码后的图片共享视图
    uint64_t contentRevision = 0;              ///< 资产内容版本，同 key 版本变化时替换 GPU 贴图
    bool srgb = false;                         ///< sRGB 意图，由 material slot 决定
    bool generateMips = true;                  ///< 是否生成 mip 链，属于 GPU 资源身份的一部分
};

/// 无显式材质时的内置资源身份；不依赖 RenderWorld generation，可跨重建稳定复用。
inline constexpr RenderResourceKey defaultRenderMaterialResourceKey() {
    return makeRenderResourceKey(builtinResourceDomain(), 1, RenderResourceKind::Builtin);
}

struct RenderMaterialDesc {
    RenderResourceKey resourceKey;
    Material material = Material::defaultPBR();
    RenderTextureDesc baseColorTexture;
    RenderTextureDesc normalTexture;
    RenderTextureDesc metallicRoughnessTexture;
    RenderTextureDesc emissiveTexture;
    RenderTextureDesc ambientOcclusionTexture;
};

struct RenderGeometryDesc {
    /// 资产身份 key（由 view 层 RenderWorldSync 按资产身份生成，engine 只校验有效性并透传）。
    /// 用作 AssetGpuRegistry 的去重/查表 key，跨帧稳定。
    RenderResourceKey resourceKey;

    /// 冗余标量：渲染端常用，避免每次都解引用 mesh。snapshot 拷贝的是这些标量，零成本。
    graphics::PrimitiveTopology topology = graphics::PrimitiveTopology::TriangleList;
    graphics::VertexLayout vertexLayout;
    bool empty = true;
};

struct RenderObjectDrawable {
    GeometryHandle geometry;
    RenderMaterialHandle material;
    RenderBucket bucket = RenderBucket::Surface;
    size_t sourceDrawableIndex = 0;
};

struct RenderObjectDesc {
    PickId pickId;
    math::Mat4 worldTransform{ 1.0f };
    math::AABB3 worldBounds;
    std::vector<RenderObjectDrawable> drawables;
    bool visible = true;
    bool selected = false;
};

struct RenderGeometryRecord {
    GeometryHandle handle;
    RenderGeometryDesc desc;
};

struct RenderMaterialRecord {
    RenderMaterialHandle handle;
    RenderMaterialDesc desc;
};

struct RenderObjectRecord {
    RenderObjectId id;
    RenderObjectDesc desc;
};

}  // namespace mulan::engine
