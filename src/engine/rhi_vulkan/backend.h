/**
 * @file backend.h
 * @brief Vulkan RHI 后端模块入口
 * @author hxxcxx
 * @date 2026-07-14
 */

#pragma once

namespace mulan::engine {

struct BackendModule;

const BackendModule& vulkanBackendModule();

}  // namespace mulan::engine
