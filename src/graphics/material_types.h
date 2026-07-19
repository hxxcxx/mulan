/**
 * @file material_types.h
 * @brief 定义 graphics/asset/engine 共享的材质枚举和值类型。
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include <cstdint>
#include <string>

namespace mulan::graphics {

/// 材质的光照语义。该值从文件导入一直传递到渲染管线选择，不能由视图模式猜测。
enum class MaterialShadingModel : uint8_t {
    Unlit,
    Lambert,
    BlinnPhong,
    MetallicRoughness,
};

inline const char* materialShadingModelToString(MaterialShadingModel model) {
    switch (model) {
    case MaterialShadingModel::Unlit: return "Unlit";
    case MaterialShadingModel::Lambert: return "Lambert";
    case MaterialShadingModel::BlinnPhong: return "BlinnPhong";
    case MaterialShadingModel::MetallicRoughness: return "MetallicRoughness";
    }
    return "MetallicRoughness";
}

inline MaterialShadingModel materialShadingModelFromString(const std::string& value) {
    if (value == "Unlit" || value == "unlit")
        return MaterialShadingModel::Unlit;
    if (value == "Lambert" || value == "lambert")
        return MaterialShadingModel::Lambert;
    if (value == "BlinnPhong" || value == "blinnphong")
        return MaterialShadingModel::BlinnPhong;
    return MaterialShadingModel::MetallicRoughness;
}

enum class AlphaMode : uint8_t {
    Opaque,
    Mask,
    Blend,
};

inline const char* alphaModeToString(AlphaMode m) {
    switch (m) {
    case AlphaMode::Opaque: return "Opaque";
    case AlphaMode::Mask: return "Mask";
    case AlphaMode::Blend: return "Blend";
    }
    return "Opaque";
}

inline AlphaMode alphaModeFromString(const std::string& s) {
    if (s == "Mask" || s == "mask")
        return AlphaMode::Mask;
    if (s == "Blend" || s == "blend")
        return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

}  // namespace mulan::graphics
