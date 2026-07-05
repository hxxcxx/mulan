/**
 * @file vk_debug_name.h
 * @brief Vulkan 对象命名工具 — 为 GPU 资源挂调试名（RenderDoc / Nsight / Validation 可见）
 *
 * 用法：在资源构造、句柄创建完成后调用：
 *   setDebugName(device, vk::ObjectType::eBuffer,
 *                (uint64_t)(VkBuffer)buf, "VertexBuffer[mesh]");
 *
 * 命名后，在 RenderDoc 等工具的资源浏览器里可直接按名字定位对象，
 * device 析构时若有未释放对象，validation layer 也会带名字报错。
 *
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "vk_common.h"

#include <string>
#include <string_view>

namespace mulan::engine {

/// 为任意 Vulkan 句柄挂调试名（仅在启用 VK_EXT_debug_utils 扩展时生效，零其它开销）
inline void setDebugName(vk::Device device, vk::ObjectType type, uint64_t handle, std::string_view name) {
    // 仅当 instance 启用了 VK_EXT_debug_utils 时 dispatcher 才有此函数指针；
    // 否则跳过（validation 关闭时扩展未启用）
    if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkSetDebugUtilsObjectNameEXT)
        return;
    if (name.empty() || handle == 0)
        return;
    vk::DebugUtilsObjectNameInfoEXT info;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.data();
    device.setDebugUtilsObjectNameEXT(info);
}

}  // namespace mulan::engine
