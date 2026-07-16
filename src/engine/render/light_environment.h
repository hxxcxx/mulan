/**
 * @file light_environment.h
 * @brief 定义渲染光源、模型查看灯策略与全局光照环境参数。
 * @author hxxcxx
 * @date 2026-04-17
 *
 * 设计思路：
 * 本文件只描述 CPU 侧光照语义。GPU 常量布局统一定义在 gpu_scene_contract.h，
 * 防止场景常量与独立光源结构产生两套互相漂移的协议。
 */

#pragma once

#include <mulan/math/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mulan::engine {

// ============================================================
// 光源类型
// ============================================================

enum class LightType : uint8_t {
    Directional,  ///< 平行光（太阳光）
    Point,        ///< 点光源
    Spot,         ///< 聚光灯
};

/// 场景光源和模型查看灯的组合策略。默认值保持既有“无灯时使用查看灯”的效果。
enum class LightingMode : uint8_t {
    SceneWithViewerFallback,  ///< 有场景灯时使用场景灯，否则使用相机跟随查看灯
    ViewerDefault,            ///< 始终使用查看灯，忽略场景灯
    SceneOnly,                ///< 只使用场景灯；空场景允许没有直接光
    Hybrid,                   ///< 场景灯与查看灯共同参与，查看灯占用一个灯位
};

// ============================================================
// 光源描述 — CPU 端
// ============================================================

struct Light {
    LightType type = LightType::Directional;

    math::Vec3 color = { 1.0, 1.0, 1.0 };
    double intensity = 1.0;

    // Directional: 光照方向（从光源指向场景）
    math::Vec3 direction = math::Vec3{ -0.3, -1.0, -0.4 }.normalized();

    // Point / Spot: 光源位置
    math::Vec3 position = math::Vec3(0.0);

    // Point / Spot: 衰减范围
    double range = 10.0;

    // Spot: 锥形角度 (弧度)
    double innerConeAngle = 0.5235987755982988;  // 30°
    double outerConeAngle = 0.7853981633974483;  // 45°

    bool castShadow = false;

    // --- 便捷工厂 ---

    static Light directional(const math::Vec3& dir, const math::Vec3& color = { 1, 1, 1 }, double intensity = 1.0) {
        Light l;
        l.type = LightType::Directional;
        l.direction = dir.normalized();
        l.color = color;
        l.intensity = intensity;
        return l;
    }

    static Light point(const math::Vec3& pos, double range, const math::Vec3& color = { 1, 1, 1 },
                       double intensity = 1.0) {
        Light l;
        l.type = LightType::Point;
        l.position = pos;
        l.range = range;
        l.color = color;
        l.intensity = intensity;
        return l;
    }

    static Light spot(const math::Vec3& pos, const math::Vec3& dir, double range, double innerConeAngle,
                      double outerConeAngle, const math::Vec3& color = { 1, 1, 1 }, double intensity = 1.0) {
        Light l = point(pos, range, color, intensity);
        l.type = LightType::Spot;
        l.direction = dir.normalizedOr(math::Vec3(0.0, 0.0, -1.0));
        l.innerConeAngle = innerConeAngle;
        l.outerConeAngle = outerConeAngle;
        return l;
    }

    /// 在进入渲染快照时统一清理 NaN、负能量和无效锥角，避免污染整帧颜色。
    Light sanitized() const {
        constexpr double kHalfPi = 1.5707963267948966;
        const auto finiteOr = [](double value, double fallback) {
            return std::isfinite(value) ? value : fallback;
        };
        const auto nonNegative = [&](double value) {
            return std::max(0.0, finiteOr(value, 0.0));
        };

        Light result = *this;
        switch (result.type) {
        case LightType::Directional:
        case LightType::Point:
        case LightType::Spot: break;
        default: result.type = LightType::Directional; break;
        }
        result.position = {
            finiteOr(position.x, 0.0),
            finiteOr(position.y, 0.0),
            finiteOr(position.z, 0.0),
        };
        result.color = {
            nonNegative(color.x),
            nonNegative(color.y),
            nonNegative(color.z),
        };
        result.intensity = nonNegative(intensity);
        result.range = nonNegative(range);
        const math::Vec3 finiteDirection{
            finiteOr(direction.x, 0.0),
            finiteOr(direction.y, 0.0),
            finiteOr(direction.z, 0.0),
        };
        result.direction = finiteDirection.normalizedOr(math::Vec3(0.0, 0.0, -1.0));
        result.outerConeAngle = std::clamp(finiteOr(outerConeAngle, 0.7853981633974483), 0.0, kHalfPi);
        result.innerConeAngle = std::clamp(finiteOr(innerConeAngle, 0.0), 0.0, result.outerConeAngle);
        return result;
    }
};

// ============================================================
// 场景光照环境 — 汇聚所有活跃光照数据
// ============================================================

struct LightEnvironment {
    static constexpr uint32_t kMaxLights = 8;

    Light lights[kMaxLights];
    uint32_t lightCount = 0;
    LightingMode mode = LightingMode::SceneWithViewerFallback;

    // --- 环境光 ---
    math::Vec3 ambientColor = { 0.15, 0.15, 0.18 };
    double ambientIntensity = 1.0;

    // --- 天空 (IBL 相关预留) ---
    double exposure = 1.0;

    void clear() { *this = {}; }

    void clearLights() { lightCount = 0; }
    bool hasSceneLights() const { return lightCount != 0; }

    uint32_t addLight(const Light& light) {
        if (lightCount >= kMaxLights)
            return kMaxLights;
        lights[lightCount] = light.sanitized();
        return lightCount++;
    }

    /// 获取主方向光（第一个 Directional），不存在则返回 nullptr
    const Light* primaryDirectional() const {
        for (uint32_t i = 0; i < std::min(lightCount, kMaxLights); ++i) {
            if (lights[i].type == LightType::Directional)
                return &lights[i];
        }
        return nullptr;
    }
};

/// 一帧实际参与着色的不可变光照快照；已完成查看灯策略选择和参数清洗。
struct ResolvedLighting {
    Light lights[LightEnvironment::kMaxLights];
    uint32_t lightCount = 0;
    math::Vec3 ambientColor{ 0.15, 0.15, 0.18 };
    double exposure = 1.0;
};

/// 根据相机和策略解析最终灯光。纯函数，可在 owner/render 线程之间按值传递和测试。
inline ResolvedLighting resolveLighting(const LightEnvironment& environment, const math::Mat4& viewMatrix) {
    ResolvedLighting result;
    const auto append = [&](const Light& light) {
        if (result.lightCount < LightEnvironment::kMaxLights)
            result.lights[result.lightCount++] = light.sanitized();
    };
    const auto appendSceneLights = [&]() {
        for (uint32_t index = 0; index < std::min(environment.lightCount, LightEnvironment::kMaxLights); ++index)
            append(environment.lights[index]);
    };
    const auto viewerDefaultLight = [&]() {
        // 与既有模型查看效果保持相同：相机空间左上前方的柔和主光。
        const math::Vec3 viewLightDirection = math::Vec3(0.35, -0.45, -0.82).normalized();
        const math::Vec3 worldDirection =
                (math::Mat3(viewMatrix).transposed() * viewLightDirection).normalizedOr(math::Vec3(0.0, 0.0, -1.0));
        return Light::directional(worldDirection, math::Vec3(0.95, 0.94, 0.92));
    };

    switch (environment.mode) {
    case LightingMode::ViewerDefault: append(viewerDefaultLight()); break;
    case LightingMode::SceneOnly: appendSceneLights(); break;
    case LightingMode::Hybrid:
        append(viewerDefaultLight());
        appendSceneLights();
        break;
    case LightingMode::SceneWithViewerFallback:
        if (environment.hasSceneLights())
            appendSceneLights();
        else
            append(viewerDefaultLight());
        break;
    default: append(viewerDefaultLight()); break;
    }

    const auto nonNegativeFinite = [](double value) {
        return std::isfinite(value) ? std::max(0.0, value) : 0.0;
    };
    result.ambientColor = {
        nonNegativeFinite(environment.ambientColor.x) * nonNegativeFinite(environment.ambientIntensity),
        nonNegativeFinite(environment.ambientColor.y) * nonNegativeFinite(environment.ambientIntensity),
        nonNegativeFinite(environment.ambientColor.z) * nonNegativeFinite(environment.ambientIntensity),
    };
    // 默认环境参数负责无配置场景的可见性；显式零值必须能够真正关闭环境光。
    result.exposure = std::isfinite(environment.exposure) ? std::max(0.0, environment.exposure) : 1.0;
    return result;
}

}  // namespace mulan::engine
