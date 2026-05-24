/**
 * @file LightEnvironment.h
 * @brief 光源与全局光照环境参数
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 *  - DirectionalLight / PointLight / SpotLight 三种光源
 *  - LightEnvironment 汇聚场景中所有活跃光源
 *  - LightGPU / SceneLightGPU 是对应的 GPU 常量布局
 */

#pragma once

#include "../math/Math.h"

#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mulan::engine {

// ============================================================
// 光源类型
// ============================================================

enum class LightType : uint8_t {
    Directional,    ///< 平行光（太阳光）
    Point,          ///< 点光源
    Spot,           ///< 聚光灯
};

// ============================================================
// 光源描述 — CPU 端
// ============================================================

struct Light {
    LightType type = LightType::Directional;

    Vec3   color     = {1.0, 1.0, 1.0};
    double intensity = 1.0;

    // Directional: 光照方向（从光源指向场景）
    Vec3   direction = glm::normalize(Vec3{-0.3, -1.0, -0.4});

    // Point / Spot: 光源位置
    Vec3   position  = Vec3(0.0);

    // Point / Spot: 衰减范围
    double range     = 10.0;

    // Spot: 锥形角度 (弧度)
    double innerConeAngle = M_PI / 6.0;   // 30°
    double outerConeAngle = M_PI / 4.0;   // 45°

    bool   castShadow = false;

    // --- 便捷工厂 ---

    static Light directional(const Vec3& dir, const Vec3& color = {1,1,1}, double intensity = 1.0) {
        Light l;
        l.type = LightType::Directional;
        l.direction = glm::normalize(dir);
        l.color = color;
        l.intensity = intensity;
        return l;
    }

    static Light point(const Vec3& pos, double range, const Vec3& color = {1,1,1}, double intensity = 1.0) {
        Light l;
        l.type = LightType::Point;
        l.position = pos;
        l.range = range;
        l.color = color;
        l.intensity = intensity;
        return l;
    }
};

// ============================================================
// 场景光照环境 — 汇聚所有活跃光照数据
// ============================================================

struct LightEnvironment {
    static constexpr uint32_t kMaxLights = 8;

    Light    lights[kMaxLights];
    uint32_t lightCount = 0;

    // --- 环境光 ---
    Vec3   ambientColor    = {0.15, 0.15, 0.18};
    double ambientIntensity = 1.0;

    // --- 天空 (IBL 相关预留) ---
    double exposure = 1.0;

    void clear() {
        lightCount = 0;
        ambientColor = {0.15, 0.15, 0.18};
        ambientIntensity = 1.0;
    }

    uint32_t addLight(const Light& light) {
        if (lightCount >= kMaxLights) return kMaxLights;
        lights[lightCount] = light;
        return lightCount++;
    }

    /// 获取主方向光（第一个 Directional），不存在则返回 nullptr
    const Light* primaryDirectional() const {
        for (uint32_t i = 0; i < lightCount; ++i) {
            if (lights[i].type == LightType::Directional) return &lights[i];
        }
        return nullptr;
    }
};

// ============================================================
// GPU 端光源布局 (std140)
// ============================================================

struct alignas(16) LightGPU {
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

static_assert(sizeof(LightGPU) == 64, "LightGPU must be 64 bytes (4 x vec4)");

// ============================================================
// GPU 端场景光照环境布局
// ============================================================

struct alignas(16) SceneLightGPU {
    static constexpr uint32_t kMaxLightsGPU = 8;

    LightGPU lights[kMaxLightsGPU];

    float    ambientColor[3];
    float    ambientIntensity;

    uint32_t lightCount;
    float    exposure;
    float    _pad[2];

    static SceneLightGPU fromEnvironment(const LightEnvironment& env) {
        SceneLightGPU g{};
        g.lightCount = std::min(env.lightCount, kMaxLightsGPU);
        for (uint32_t i = 0; i < g.lightCount; ++i) {
            const auto& src = env.lights[i];
            auto& dst = g.lights[i];
            dst.position[0]  = static_cast<float>(src.position.x);
            dst.position[1]  = static_cast<float>(src.position.y);
            dst.position[2]  = static_cast<float>(src.position.z);
            dst.range         = static_cast<float>(src.range);
            dst.direction[0] = static_cast<float>(src.direction.x);
            dst.direction[1] = static_cast<float>(src.direction.y);
            dst.direction[2] = static_cast<float>(src.direction.z);
            dst.intensity     = static_cast<float>(src.intensity);
            dst.color[0]     = static_cast<float>(src.color.x);
            dst.color[1]     = static_cast<float>(src.color.y);
            dst.color[2]     = static_cast<float>(src.color.z);
            dst.type          = static_cast<uint32_t>(src.type);
            dst.innerConeAngle = static_cast<float>(src.innerConeAngle);
            dst.outerConeAngle = static_cast<float>(src.outerConeAngle);
            dst.castShadow     = src.castShadow ? 1u : 0u;
        }
        g.ambientColor[0]   = static_cast<float>(env.ambientColor.x);
        g.ambientColor[1]   = static_cast<float>(env.ambientColor.y);
        g.ambientColor[2]   = static_cast<float>(env.ambientColor.z);
        g.ambientIntensity   = static_cast<float>(env.ambientIntensity);
        g.exposure           = static_cast<float>(env.exposure);
        return g;
    }
};

static_assert(sizeof(SceneLightGPU) == 544, "SceneLightGPU = 8*64 + 32 = 544");

} // namespace mulan::Engine
