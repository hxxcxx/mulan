/**
 * @file command_source.h
 * @brief 区分下发到 Stage 的绘制命令来源（场景 / 叠加层）。
 * @author hxxcxx
 * @date 2026-07-21
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

enum class CommandSource : uint8_t {
    Scene,
    Overlay,
};

}  // namespace mulan::engine
