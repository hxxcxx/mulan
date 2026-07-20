/**
 * @file gpu_scene_contract.h
 * @brief 定义 CPU 与着色器共享的场景和对象 Uniform 数据契约
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "light_environment.h"
#include "material/material.h"

#include <mulan/math/math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mulan::engine {

/// 对应 shaders/common.slang 中的 Material 常量缓冲（b2）。
struct alignas(16) MaterialGPU {
    float baseColor[3];
    float metallic;
    float emissive[3];
    float roughness;
    float specular[3];
    float shininess;
    float alpha;
    float ao;
    float emissiveStrength;
    float alphaCutoff;
    uint32_t shadingModel;
    uint32_t alphaMode;
    uint32_t textureFlags;
    uint32_t doubleSided;
    float ambient[3];
    float reserved = 0.0f;

    static constexpr size_t kSize = 96;

    static MaterialGPU fromMaterial(const Material& material) {
        MaterialGPU result{};
        result.baseColor[0] = static_cast<float>(material.baseColor.x);
        result.baseColor[1] = static_cast<float>(material.baseColor.y);
        result.baseColor[2] = static_cast<float>(material.baseColor.z);
        result.metallic = static_cast<float>(std::clamp(material.metallic, 0.0, 1.0));
        result.emissive[0] = static_cast<float>(material.emissive.x * material.emissiveStrength);
        result.emissive[1] = static_cast<float>(material.emissive.y * material.emissiveStrength);
        result.emissive[2] = static_cast<float>(material.emissive.z * material.emissiveStrength);
        result.roughness = static_cast<float>(std::clamp(material.roughness, 0.04, 1.0));
        result.specular[0] = static_cast<float>(material.specular.x);
        result.specular[1] = static_cast<float>(material.specular.y);
        result.specular[2] = static_cast<float>(material.specular.z);
        result.shininess = static_cast<float>(material.shininess);
        result.alpha = static_cast<float>(material.alpha);
        result.ao = static_cast<float>(material.ao);
        result.emissiveStrength = static_cast<float>(material.emissiveStrength);
        result.alphaCutoff = static_cast<float>(material.alphaCutoff);
        result.shadingModel = static_cast<uint32_t>(material.shadingModel);
        result.alphaMode = static_cast<uint32_t>(material.alphaMode);
        result.textureFlags = static_cast<uint32_t>(material.textureSlots);
        result.doubleSided = material.doubleSided ? 1u : 0u;
        result.ambient[0] = static_cast<float>(material.ambient.x);
        result.ambient[1] = static_cast<float>(material.ambient.y);
        result.ambient[2] = static_cast<float>(material.ambient.z);
        return result;
    }
};
static_assert(sizeof(MaterialGPU) == MaterialGPU::kSize, "MaterialGPU must match shaders/common.slang");

static_assert(static_cast<uint32_t>(LightType::Directional) == 0u);
static_assert(static_cast<uint32_t>(LightType::Point) == 1u);
static_assert(static_cast<uint32_t>(LightType::Spot) == 2u);

/// 对应 shaders/common.slang 的 SceneLight；四个 16-byte 槽位，禁止使用 bool。
struct alignas(16) SceneLightUniform {
    float position[3];
    float range;
    float direction[3];
    float intensity;
    float color[3];
    uint32_t type;
    float innerConeAngle;
    float outerConeAngle;
    uint32_t castShadow;
    float _pad;
};
static_assert(sizeof(SceneLightUniform) == 64);

// 对应 shaders/common.slang 中的 Scene 常量缓冲（b0）。前 288 字节保留旧布局，
// 让 ViewCube 等只消费公共相机字段的管线无需建立第二份场景常量协议。
struct alignas(16) SceneUniforms {
    float view[16];
    float projection[16];
    float viewProjection[16];
    float cameraPos[4];
    float lightDir[4];
    float lightColor[4];
    float ambientColor[4];
    float edgeColor[4];
    float highlightColor[4];
    SceneLightUniform lights[LightEnvironment::kMaxLights];
    uint32_t lightCount;
    float exposure;
    float _pad[2];
};
static_assert(offsetof(SceneUniforms, lights) == 288);
static_assert(offsetof(SceneUniforms, lightCount) == 800);
static_assert(offsetof(SceneUniforms, exposure) == 804);
static_assert(sizeof(SceneUniforms) == 816);

// 对应 shaders/common.slang 中的 Object 常量缓冲（b1）。
struct alignas(16) ObjectUniforms {
    float world[16];
    float normalMat[12];
    uint32_t pickId;
    uint32_t selected;
    uint32_t hovered;
    uint32_t _pad;
};
static_assert(offsetof(ObjectUniforms, world) == 0);
static_assert(offsetof(ObjectUniforms, normalMat) == 64);
static_assert(offsetof(ObjectUniforms, pickId) == 112);
static_assert(offsetof(ObjectUniforms, selected) == 116);
static_assert(offsetof(ObjectUniforms, hovered) == 120);
static_assert(offsetof(ObjectUniforms, _pad) == 124);
static_assert(sizeof(ObjectUniforms) == 128);

/// 所有后端共同保证至少 16 KiB 的单次 UniformBuffer 绑定范围。
/// 固定容量避免 Vulkan descriptor range 抖动，也满足 DX11/GL 对完整 cbuffer/block 的校验。
inline constexpr uint32_t kObjectBatchCapacity = 128;

/// 对应实例化顶点着色器 binding=1 的 ObjectBatch 常量缓冲。
struct alignas(16) ObjectBatchUniforms {
    ObjectUniforms objects[kObjectBatchCapacity];
};
static_assert(offsetof(ObjectBatchUniforms, objects) == 0);
static_assert(sizeof(ObjectBatchUniforms) == 16u * 1024u);

inline void storeGpuMat4(float* dst, const math::Mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            dst[c * 4 + r] = static_cast<float>(m[c][r]);
        }
    }
}

inline void storeGpuMat3x4(float* dst, const math::Mat3& m) {
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            dst[c * 4 + r] = static_cast<float>(m[c][r]);
        }
    }
}

/// 将 CPU double 安全收敛到 GPU float 域，避免有限超大值在窄化时变成 Infinity。
inline float toFiniteGpuFloat(double value) {
    constexpr double kLimit = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(value))
        return 0.0f;
    return static_cast<float>(std::clamp(value, -kLimit, kLimit));
}

inline void storeGpuVec3(float* dst, const math::Vec3& v) {
    dst[0] = toFiniteGpuFloat(v.x);
    dst[1] = toFiniteGpuFloat(v.y);
    dst[2] = toFiniteGpuFloat(v.z);
    dst[3] = 0.0f;
}

inline SceneLightUniform makeSceneLightUniform(const Light& source) {
    const Light light = source.sanitized();
    SceneLightUniform result{};
    result.position[0] = toFiniteGpuFloat(light.position.x);
    result.position[1] = toFiniteGpuFloat(light.position.y);
    result.position[2] = toFiniteGpuFloat(light.position.z);
    result.range = toFiniteGpuFloat(light.range);
    result.direction[0] = toFiniteGpuFloat(light.direction.x);
    result.direction[1] = toFiniteGpuFloat(light.direction.y);
    result.direction[2] = toFiniteGpuFloat(light.direction.z);
    result.intensity = toFiniteGpuFloat(light.intensity);
    result.color[0] = toFiniteGpuFloat(light.color.x);
    result.color[1] = toFiniteGpuFloat(light.color.y);
    result.color[2] = toFiniteGpuFloat(light.color.z);
    result.type = static_cast<uint32_t>(light.type);
    result.innerConeAngle = toFiniteGpuFloat(light.innerConeAngle);
    result.outerConeAngle = toFiniteGpuFloat(light.outerConeAngle);
    result.castShadow = light.castShadow ? 1u : 0u;
    return result;
}

inline ObjectUniforms makeObjectUniforms(const math::Mat4& world, uint32_t pickId = 0, bool selected = false,
                                         bool hovered = false) {
    ObjectUniforms ubo{};
    storeGpuMat4(ubo.world, world);

    const math::Mat3 upper(world);
    const math::Mat3 normalMat = upper.inverse().transposed();
    storeGpuMat3x4(ubo.normalMat, normalMat);

    ubo.pickId = pickId;
    ubo.selected = selected ? 1u : 0u;
    ubo.hovered = hovered ? 1u : 0u;
    return ubo;
}

}  // namespace mulan::engine
