/**
 * @file gpu_scene_contract.h
 * @brief 定义 CPU 与着色器共享的场景和对象 Uniform 数据契约
 * @author hxxcxx
 * @date 2026-07-05
 */

#pragma once

#include "light_environment.h"

#include <mulan/math/math.h>

#include <cstddef>
#include <cstdint>

namespace mulan::engine {

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
static_assert(sizeof(ObjectUniforms) == 128);

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

inline void storeGpuVec3(float* dst, const math::Vec3& v) {
    dst[0] = static_cast<float>(v.x);
    dst[1] = static_cast<float>(v.y);
    dst[2] = static_cast<float>(v.z);
    dst[3] = 0.0f;
}

inline SceneLightUniform makeSceneLightUniform(const Light& source) {
    const Light light = source.sanitized();
    SceneLightUniform result{};
    result.position[0] = static_cast<float>(light.position.x);
    result.position[1] = static_cast<float>(light.position.y);
    result.position[2] = static_cast<float>(light.position.z);
    result.range = static_cast<float>(light.range);
    result.direction[0] = static_cast<float>(light.direction.x);
    result.direction[1] = static_cast<float>(light.direction.y);
    result.direction[2] = static_cast<float>(light.direction.z);
    result.intensity = static_cast<float>(light.intensity);
    result.color[0] = static_cast<float>(light.color.x);
    result.color[1] = static_cast<float>(light.color.y);
    result.color[2] = static_cast<float>(light.color.z);
    result.type = static_cast<uint32_t>(light.type);
    result.innerConeAngle = static_cast<float>(light.innerConeAngle);
    result.outerConeAngle = static_cast<float>(light.outerConeAngle);
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
