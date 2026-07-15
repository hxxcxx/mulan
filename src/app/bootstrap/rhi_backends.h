/**
 * @file rhi_backends.h
 * @brief 应用实际链接的 RHI 后端组装入口
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include <mulan/core/result/error.h>

namespace mulan::engine {
class DeviceFactory;
}

namespace mulan::app {

Result<void> registerLinkedRHIBackends(engine::DeviceFactory& factory);

}  // namespace mulan::app
