/**
 * @file dx12_debug_name.h
 * @brief D3D12 对象命名工具 — 为 GPU 资源挂调试名（PIX / RenderDoc / Visual Studio Graphics Diagnostics 可见）
 *
 * D3D12 的 ID3D12Object::SetName 是原生 API（不像 Vulkan 需要扩展），
 * 任何配置下调用都安全，零 Release 开销（驱动内部存一个字符串）。
 *
 * 命名后，在 PIX、Visual Studio Graphics Diagnostics、RenderDoc 的
 * 资源浏览器里可直接按名字定位对象。
 *
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "dx12_common.h"

#include <string>
#include <string_view>

namespace mulan::engine {

/// 为任意 ID3D12Object 挂调试名（接受 UTF-8/窄字符串，内部转宽字符）
inline void setDebugName(ID3D12Object* obj, std::string_view name) {
    if (!obj || name.empty()) return;
    // D3D12 SetName 接受宽字符串；此处做窄→宽转换
    std::wstring wname(name.begin(), name.end());
    obj->SetName(wname.c_str());
}

} // namespace mulan::engine
