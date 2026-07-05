/**
 * @file render_object.h
 * @brief 定义 engine frontend 的渲染对象、材质、纹理和几何描述。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "render_handle.h"
#include "../material/material.h"

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstddef>
#include <cstdint>
#include <string>
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
    std::string sourcePath;               ///< 文件路径（文件源）或缓存键（内嵌源）
    std::vector<std::byte> embeddedData;  ///< 内嵌编码字节；非空时优先于 sourcePath
    bool srgb = false;                    ///< sRGB 意图，由 material slot 决定
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
    graphics::Mesh mesh;
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
