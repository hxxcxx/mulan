/**
 * @file Sampler.h
 * @brief 采样器资源基类
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "RenderState.h"

#include <cstdint>

namespace MulanGeo::engine {

class Sampler {
public:
    virtual ~Sampler() = default;

    virtual const SamplerDesc& desc() const = 0;

protected:
    Sampler() = default;
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
};

} // namespace MulanGeo::Engine
