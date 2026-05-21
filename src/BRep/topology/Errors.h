/**
 * @file Errors.h
 * @brief 拓扑错误类型枚举
 *
 * 基于 truck-topology::errors。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include <cstdint>

namespace MulanGeo::BRep {

enum class TopologyError : uint8_t {
    SameVertex,          // Edge 两端点相同
    EmptyWire,           // Wire 为空
    NotClosedWire,       // Wire 首尾不闭合
    NotSimpleWire,       // Wire 存在自交
    NotDisjointWires,    // Face 的多个边界线相交
    EmptyShell,          // Shell 为空
    NotConnected,        // Shell 不连通
    NotClosedShell,      // Shell 不闭合
    NotManifold,         // Shell 非流形
};

} // namespace MulanGeo::BRep
