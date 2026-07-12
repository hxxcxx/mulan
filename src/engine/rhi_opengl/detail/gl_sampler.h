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

    const SamplerDesc& desc() const override { return desc_; }

    GLuint handle() const { return handle_; }
    bool isValid() const { return handle_ != 0; }

private:
    SamplerDesc desc_;
    GLuint handle_ = 0;
};

}  // namespace mulan::engine
