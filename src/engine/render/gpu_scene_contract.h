#pragma once

#include <mulan/math/math.h>

#include <cstdint>

namespace mulan::engine {

// C++ mirror of shaders/common.hlsli cbuffer Scene (b0).
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
};
static_assert(sizeof(SceneUniforms) == 288);

// C++ mirror of shaders/common.hlsli cbuffer Object (b1).
struct alignas(16) ObjectUniforms {
    float world[16];
    float normalMat[12];
    uint32_t pickId;
    uint32_t selected;
    float _pad[2];
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

inline ObjectUniforms makeObjectUniforms(const math::Mat4& world,
                                         uint32_t pickId = 0,
                                         bool selected = false) {
    ObjectUniforms ubo{};
    storeGpuMat4(ubo.world, world);

    const math::Mat3 upper(world);
    const math::Mat3 normalMat = upper.inverse().transposed();
    storeGpuMat3x4(ubo.normalMat, normalMat);

    ubo.pickId = pickId;
    ubo.selected = selected ? 1u : 0u;
    return ubo;
}

} // namespace mulan::engine
