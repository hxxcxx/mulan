/**
 * @file submission.h
 * @brief 跨后端 GPU 队列提交完成标识。
 */
#pragma once

#include <cstdint>

namespace mulan::engine {

enum class QueueType : uint8_t {
    Graphics,
    Compute,
    Copy,
};

/// 轻量、无所有权的提交标识。deviceGeneration 防止后端重建后误用旧 token。
struct SubmissionToken {
    uint64_t deviceGeneration = 0;
    QueueType queue = QueueType::Graphics;
    uint64_t value = 0;

    explicit operator bool() const { return deviceGeneration != 0 && value != 0; }
};

}  // namespace mulan::engine
