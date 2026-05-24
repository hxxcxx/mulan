/**
 * @file Errors.h
 * @brief 拓扑错误类型枚举 + core::Error 转换
 *
 * 拓扑错误统一通过 core::Error 传播：
 *   code    = TopologyError 枚举值
 *   message = 人类可读描述（自动填充）
 *   file/line = 由 source_location 自动捕获
 *
 * 用法:
 *   return core::Err<T>(makeError(TopologyError::EmptyWire));
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include <mulan/Core/result/Result.h>
#include <cstdint>
#include <string_view>

namespace mulan::brep {

enum class TopologyError : int32_t {
    SameVertex,          // Edge 两端点相同
    EmptyWire,           // Wire 为空
    NotClosedWire,       // Wire 首尾不闭合
    NotSimpleWire,       // Wire 存在自交
    NotDisjointWires,    // Face 的多个边界线相交
    EmptyShell,          // Shell 为空
    NotConnected,        // Shell 不连通
    NotClosedShell,      // Shell 不闭合
    NotManifold,         // Shell 非流形
    InvalidParameter,    // 参数不在有效范围内
};

/// 拓扑错误描述文本
constexpr std::string_view toString(TopologyError e) noexcept {
    switch (e) {
    case TopologyError::SameVertex:       return "edge endpoints are the same vertex";
    case TopologyError::EmptyWire:        return "wire is empty";
    case TopologyError::NotClosedWire:    return "wire is not closed";
    case TopologyError::NotSimpleWire:    return "wire is not simple (self-intersecting)";
    case TopologyError::NotDisjointWires: return "face boundaries are not disjoint";
    case TopologyError::EmptyShell:       return "shell is empty";
    case TopologyError::NotConnected:     return "shell is not connected";
    case TopologyError::NotClosedShell:   return "shell is not closed";
    case TopologyError::NotManifold:      return "shell is not manifold";
    case TopologyError::InvalidParameter: return "parameter out of valid range";
    }
    return "unknown topology error";
}

/// TopologyError → core::Error 转换（自动捕获调用位置）
inline core::Error makeError(TopologyError e,
    std::source_location loc = std::source_location::current()) {
    return core::Error::make(static_cast<int32_t>(e), toString(e), loc);
}

} // namespace mulan::BRep
