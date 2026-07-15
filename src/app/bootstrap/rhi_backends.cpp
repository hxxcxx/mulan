/**
 * @file rhi_backends.cpp
 * @brief 显式注册应用实际编译的 RHI 后端模块
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "rhi_backends.h"

#include <mulan/rhi/device_factory.h>

#if MULAN_HAS_RHI_D3D12
#include <mulan/rhi_dx12/backend.h>
#endif
#if MULAN_HAS_RHI_D3D11
#include <mulan/rhi_dx11/backend.h>
#endif
#if MULAN_HAS_RHI_VULKAN
#include <mulan/rhi_vulkan/backend.h>
#endif
#if MULAN_HAS_RHI_OPENGL
#include <mulan/rhi_opengl/backend.h>
#endif

namespace mulan::app {

Result<void> registerLinkedRHIBackends(engine::DeviceFactory& factory) {
#if MULAN_HAS_RHI_VULKAN
    if (auto result = factory.registerModule(engine::vulkanBackendModule()); !result)
        return result;
#endif
#if MULAN_HAS_RHI_D3D12
    if (auto result = factory.registerModule(engine::d3d12BackendModule()); !result)
        return result;
#endif
#if MULAN_HAS_RHI_D3D11
    if (auto result = factory.registerModule(engine::d3d11BackendModule()); !result)
        return result;
#endif
#if MULAN_HAS_RHI_OPENGL
    if (auto result = factory.registerModule(engine::openGLBackendModule()); !result)
        return result;
#endif
    return {};
}

}  // namespace mulan::app
