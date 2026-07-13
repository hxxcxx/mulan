/**
 * @file uniform_slice.h
 * @brief 瞬态 Uniform 数据切片与动态绑定描述
 * @author hxxcxx
 * @date 2026-07-13
 */

#pragma once

#include <cstdint>

namespace mulan::engine {

class Buffer;

/// CommandList 在一次录制期间写入的不可变 Uniform 数据范围。
/// backingBuffer 由后端瞬态分配器持有，调用方不得跨录制或跨帧保存该切片。
struct UniformSlice {
    Buffer* backingBuffer = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint64_t recordingGeneration = 0;

    explicit operator bool() const noexcept { return backingBuffer && size != 0 && recordingGeneration != 0; }
};

/// 将一个动态 UniformSlice 关联到 Pipeline layout 中声明为 Dynamic 的 binding。
struct DynamicUniformBinding {
    uint32_t binding = 0;
    UniformSlice slice;
};

}  // namespace mulan::engine
