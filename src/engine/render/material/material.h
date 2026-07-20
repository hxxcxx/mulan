/**
 * @file material.h
 * @brief 渲染前端使用的材质类型系统
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 *  - Material 是值类型，可直接存入容器或节点
 *  - MaterialShadingModel 标识文件材质的光照语义
 *  - 纹理槽位使用枚举 TextureSlot 统一索引
 */

#pragma once

#include <mulan/graphics/material_types.h>
#include <mulan/math/math.h>

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace mulan::engine {

using graphics::MaterialShadingModel;
using graphics::materialShadingModelFromString;
using graphics::materialShadingModelToString;

// ============================================================
// Alpha 模式 — 透明度处理策略
// ============================================================

using graphics::AlphaMode;
using graphics::alphaModeFromString;
using graphics::alphaModeToString;

// ============================================================
// 纹理槽位 — 统一索引
// ============================================================

enum class TextureSlot : uint8_t {
    Albedo = 0,
    Normal = 1,
    MetallicRoughness = 2,
    Emissive = 3,
    AO = 4,
    Ambient = 5,
    Specular = 6,
    Shininess = 7,
    Opacity = 8,
    Count = 9,
};

/// 纹理槽位名称
inline const char* textureSlotName(TextureSlot slot) {
    switch (slot) {
    case TextureSlot::Albedo: return "albedo";
    case TextureSlot::Normal: return "normal";
    case TextureSlot::MetallicRoughness: return "metallicRoughness";
    case TextureSlot::Emissive: return "emissive";
    case TextureSlot::AO: return "ao";
    case TextureSlot::Ambient: return "ambient";
    case TextureSlot::Specular: return "specular";
    case TextureSlot::Shininess: return "shininess";
    case TextureSlot::Opacity: return "opacity";
    default: return "unknown";
    }
}

// ============================================================
// 纹理槽位掩码 — 标记材质的哪些纹理槽位有数据
//
// 与 shader 的 TF_* 常量及 MaterialGPU::textureFlags 位定义一致（单一来源）：
//   bit0=albedo, bit1=normal, bit2=mr, bit3=emissive, bit4=ao
// 这里用枚举 + 位运算符替代旧的 Material::textures[]（uint16_t 数组当 bool 用，语义不清）。
// 真实纹理图像由 RenderMaterialDesc 的 RenderTextureDesc 槽位携带，本标志仅驱动 shader
// 的 (textureFlags & TF_*) 采样开关。
// ============================================================

enum class TextureSlotFlags : uint16_t {
    None = 0,
    HasAlbedo = 1u << 0,
    HasNormal = 1u << 1,
    HasMetallicRough = 1u << 2,
    HasEmissive = 1u << 3,
    HasAO = 1u << 4,
    HasAmbient = 1u << 5,
    HasSpecular = 1u << 6,
    HasShininess = 1u << 7,
    HasOpacity = 1u << 8,
};

inline TextureSlotFlags operator|(TextureSlotFlags a, TextureSlotFlags b) {
    return static_cast<TextureSlotFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline TextureSlotFlags operator&(TextureSlotFlags a, TextureSlotFlags b) {
    return static_cast<TextureSlotFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

inline TextureSlotFlags& operator|=(TextureSlotFlags& a, TextureSlotFlags b) {
    a = a | b;
    return a;
}

// ============================================================
// 材质描述 — CPU 端完整参数
// ============================================================

struct Material {
    std::string name;

    MaterialShadingModel shadingModel = MaterialShadingModel::MetallicRoughness;
    AlphaMode alphaMode = AlphaMode::Opaque;

    // --- 基础颜色（线性空间；sRGB 贴图由采样格式完成解码）---
    math::Vec3 baseColor = { 0.8, 0.8, 0.8 };
    double alpha = 1.0;

    // --- 传统光照参数 ---
    math::Vec3 ambient = { 0.0, 0.0, 0.0 };

    // --- PBR 参数 ---
    double metallic = 0.0;   ///< 金属度 [0, 1]
    double roughness = 0.5;  ///< 粗糙度 [0, 1]
    double ao = 1.0;         ///< 环境光遮蔽 [0, 1]

    // --- Blinn-Phong 参数 ---
    math::Vec3 specular = { 0.5, 0.5, 0.5 };
    double shininess = 32.0;

    // --- Emissive ---
    math::Vec3 emissive = math::Vec3(0.0);
    double emissiveStrength = 1.0;

    // --- Alpha Mask ---
    double alphaCutoff = 0.5;

    // --- 双面渲染 ---
    bool doubleSided = false;

    // --- 纹理槽位掩码：标记哪些槽位有数据，驱动 shader 的 textureFlags 采样开关 ---
    // 注：真实纹理图像与 sRGB 意图由 RenderMaterialDesc 的 RenderTextureDesc 槽位携带，
    // 这里仅是"有无"标志。详见 TextureSlotFlags。
    TextureSlotFlags textureSlots = TextureSlotFlags::None;

    // --- 便捷工厂 ---

    static Material defaultSurface() {
        Material m;
        m.name = "DefaultSurface";
        m.shadingModel = MaterialShadingModel::Lambert;
        m.ambient = m.baseColor;
        m.doubleSided = true;
        return m;
    }

    static Material defaultPBR() {
        Material m;
        m.shadingModel = MaterialShadingModel::MetallicRoughness;
        m.name = "DefaultPBR";
        return m;
    }

    static Material defaultPhong() {
        Material m;
        m.shadingModel = MaterialShadingModel::BlinnPhong;
        m.name = "DefaultPhong";
        return m;
    }

    static Material unlit(const math::Vec3& color) {
        Material m;
        m.shadingModel = MaterialShadingModel::Unlit;
        m.baseColor = color;
        m.name = "Unlit";
        return m;
    }

    static Material transparent(const math::Vec3& color, double a) {
        Material m;
        m.shadingModel = MaterialShadingModel::MetallicRoughness;
        m.alphaMode = AlphaMode::Blend;
        m.baseColor = color;
        m.alpha = a;
        m.name = "Transparent";
        return m;
    }

    /// 是否半透明
    bool isTransparent() const { return alphaMode == AlphaMode::Blend; }

    /// 是否有纹理
    bool hasAnyTexture() const { return textureSlots != TextureSlotFlags::None; }

    /// 是否等于（忽略 name）
    bool equals(const Material& o) const {
        return shadingModel == o.shadingModel && alphaMode == o.alphaMode && baseColor == o.baseColor &&
               ambient == o.ambient && alpha == o.alpha && metallic == o.metallic && roughness == o.roughness &&
               ao == o.ao && specular == o.specular && shininess == o.shininess && emissive == o.emissive &&
               emissiveStrength == o.emissiveStrength && alphaCutoff == o.alphaCutoff && doubleSided == o.doubleSided &&
               textureSlots == o.textureSlots;
    }

    bool operator==(const Material& o) const { return equals(o); }
    bool operator!=(const Material& o) const { return !equals(o); }
};

}  // namespace mulan::engine
