/**
 * @file render_object.h
 * @brief 定义 engine frontend 的渲染对象、材质、纹理和几何描述。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

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
    Overlay,
    Gizmo,
    Text,
};

struct RenderTextureDesc {
    AssetGpuKey resourceKey;                   ///< 资产身份 key，用作 GPU 贴图去重键
    std::shared_ptr<const core::Image> image;  ///< 解码后的图片共享视图
    bool srgb = false;                         ///< sRGB 意图，由 material slot 决定
};

struct RenderMaterialDesc {
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
    AssetGpuKey resourceKey;

    /// 非拥有指针，指向资产持有的 graphics::Mesh（文档存活期稳定，与 Drawable::mesh 同契约）。
    /// 仅 AssetGpuRegistry cache miss 时被读（上传 vertex/index 字节）；命中即返时不碰。
    const graphics::Mesh* mesh = nullptr;

    /// 冗余标量：渲染端常用，避免每次都解引用 mesh。snapshot 拷贝的是这些标量，零成本。
    graphics::PrimitiveTopology topology = graphics::PrimitiveTopology::TriangleList;
    bool empty = true;
};

struct RenderObjectDrawable {
    GeometryHandle geometry;
    RenderMaterialHandle material;
    RenderBucket bucket = RenderBucket::Surface;
    size_t sourceDrawableIndex = 0;
};

struct RenderObjectDesc {
    uint64_t externalId = 0;
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
