/**
 * @file parsed_scene.h
 * @brief ParsedScene —— importer 产出的中立场景结构(分区:资源池 + 节点图)。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 所有 importer 只解析文件、产出 ParsedScene,不接触 Document。
 * 由 ParsedSceneLoader 统一把 ParsedScene 灌进 Document。这样层级、变换、材质
 * 等信息在中立结构里完整保留,最后才落地。
 *
 * 分区设计:meshes/breps/lights/cameras 各自独立池,节点用索引引用哪类资源。
 *   - glTF/assimp 走 meshes 分区(带节点图)
 *   - STEP/IGES 走 breps 分区(顶层平铺节点)
 */
#pragma once

#include "import_result.h"

#include <mulan/asset/asset_id.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/graphics/material_types.h>
#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>
#include <mulan/modeling/core/shape.h>
#include <mulan/scene/components/light_component.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mulan::io {

// ============================================================
// 资源池(按类型分区,节点用索引引用)
// ============================================================

/// 纹理:源路径或已编码字节(未解码)+ MIME + 尺寸。
struct ParsedTexture {
    std::string name;
    std::string sourcePath;
    std::vector<std::byte> data;  // 编码图片字节(PNG/JPG/...);非空时优先于 sourcePath
    std::string mimeType;
    int width = 0;
    int height = 0;
};

/// 中立材质语义。PBR 与传统 MTL 参数共用此结构，纹理使用 ParsedScene.textures 索引。
struct ParsedMaterial {
    std::string name;
    graphics::MaterialShadingModel shadingModel = graphics::MaterialShadingModel::MetallicRoughness;
    math::Vec4 baseColorFactor{ 0.8, 0.8, 0.8, 1.0 };
    math::Vec3 ambientFactor{ 0.0, 0.0, 0.0 };
    math::Vec3 specularFactor{ 0.5, 0.5, 0.5 };
    double shininess = 32.0;
    double roughness = 0.5;
    double metallic = 0.0;
    size_t baseColorTexture = SIZE_MAX;
    bool baseColorTextureSrgb = true;
    size_t normalTexture = SIZE_MAX;
    bool normalTextureSrgb = false;
    size_t metallicRoughnessTexture = SIZE_MAX;
    bool metallicRoughnessTextureSrgb = false;
    size_t emissiveTexture = SIZE_MAX;
    bool emissiveTextureSrgb = true;
    size_t occlusionTexture = SIZE_MAX;
    bool occlusionTextureSrgb = false;
    size_t ambientTexture = SIZE_MAX;
    bool ambientTextureSrgb = true;
    size_t specularTexture = SIZE_MAX;
    bool specularTextureSrgb = true;
    size_t shininessTexture = SIZE_MAX;
    bool shininessTextureSrgb = false;
    size_t opacityTexture = SIZE_MAX;
    bool opacityTextureSrgb = false;
    math::Vec3 emissiveFactor{ 0.0, 0.0, 0.0 };
    double emissiveStrength = 1.0;
    graphics::AlphaMode alphaMode = graphics::AlphaMode::Opaque;
    double alphaCutoff = 0.5;
    bool doubleSided = false;
};

/// 多边形网格:多个 primitive,每个带材质索引(指向 ParsedScene.materials)。
struct ParsedMesh {
    std::string name;
    std::vector<asset::MeshPrimitive> primitives;
    std::vector<size_t> materialIndices;  // 与 primitives 等长;SIZE_MAX = 无材质
};

/// B-Rep 体:STEP/IGES 解析出的建模形状。
struct ParsedBRep {
    std::string name;
    modeling::Shape shape;
};

/// 光源:镜像 scene::LightComponent 的纯数据。
struct ParsedLight {
    scene::LightKind kind = scene::LightKind::Directional;
    math::Vec3 color{ 1.0, 1.0, 1.0 };
    double intensity = 1.0;
    double range = 0.0;
    double innerConeAngle = 0.0;
    double outerConeAngle = 0.7853981633974483;
};

// ============================================================
// 节点图
// ============================================================

/// 节点:名字 + 父子 + 本地变换 + 可选资源绑定(各分区索引,SIZE_MAX 表示无)。
struct ParsedNode {
    std::string name;
    size_t parent = SIZE_MAX;  // SIZE_MAX = 根节点
    math::Mat4 localTransform{ 1.0 };

    size_t meshIndex = SIZE_MAX;
    size_t brepIndex = SIZE_MAX;
    size_t lightIndex = SIZE_MAX;
    size_t cameraIndex = SIZE_MAX;

    bool hasMesh() const { return meshIndex != SIZE_MAX; }
    bool hasBRep() const { return brepIndex != SIZE_MAX; }
    bool hasLight() const { return lightIndex != SIZE_MAX; }
    bool isRoot() const { return parent == SIZE_MAX; }
};

// ============================================================
// ParsedScene
// ============================================================

struct ParsedScene {
    double unitScale = 1.0;

    /// importer 产生的非致命诊断，由 ParsedSceneLoader 原样并入 ImportReport。
    std::vector<std::string> warnings;

    std::vector<ParsedTexture> textures;
    std::vector<ParsedMaterial> materials;
    std::vector<ParsedMesh> meshes;
    std::vector<ParsedBRep> breps;
    std::vector<ParsedLight> lights;
    std::vector<ParsedNode> nodes;
    std::vector<size_t> rootNodes;
};

}  // namespace mulan::io
