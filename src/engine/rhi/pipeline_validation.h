/**
 * @file pipeline_validation.h
 * @brief Pipeline 创建描述的后端无关契约校验
 * @author hxxcxx
 * @date 2026-07-14
 */

#pragma once

#include "pipeline_state.h"

#include <mulan/core/result/error.h>

namespace mulan::engine {

class RHIDevice;
struct GPUDeviceCapabilities;

Result<void> validateGraphicsPipelineDesc(const GraphicsPipelineDesc& desc, const RHIDevice& device,
                                          const GPUDeviceCapabilities& capabilities);
Result<void> validateComputePipelineDesc(const ComputePipelineDesc& desc, const RHIDevice& device,
                                         const GPUDeviceCapabilities& capabilities);

}  // namespace mulan::engine
