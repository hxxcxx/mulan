/**
 * @file gl_sampler.h
 * @brief OpenGL 采样器实现
 * @author hxxcxx
 * @date 2026-04-26
 */

#pragma once

#include "../../rhi/sampler.h"
#include "gl_common.h"

namespace mulan::engine {

class GLSampler : public Sampler {
public:
    GLSampler(const SamplerDesc& desc);
    ~GLSampler();

    const SamplerDesc& desc() const override { return m_desc; }

    GLuint handle() const { return m_handle; }

private:
    SamplerDesc m_desc;
    GLuint m_handle = 0;
};

}  // namespace mulan::engine
