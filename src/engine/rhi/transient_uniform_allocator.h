/**
 * @file transient_uniform_allocator.h
 * @brief 瞬态 Uniform 线性页分配规划器
 * @author hxxcxx
 * @date 2026-07-13
 */

#pragma once

#include <cstdint>
#include <expected>

namespace mulan::engine {

enum class UniformAllocationError : uint8_t {
    InvalidConfiguration,
    RecordingNotStarted,
    EmptyAllocation,
    AllocationTooLarge,
    ArithmeticOverflow,
};

struct UniformAllocatorConfig {
    uint32_t pageSize = 64 * 1024;
    uint32_t alignment = 256;
    uint32_t maxAllocationSize = 64 * 1024;
};

struct UniformAllocationPlan {
    uint32_t pageIndex = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t reservedSize = 0;
    uint64_t recordingGeneration = 0;
};

struct UniformAllocatorStats {
    uint64_t allocationCount = 0;
    uint64_t requestedBytes = 0;
    uint64_t reservedBytes = 0;
    uint32_t pagesUsed = 0;
    uint64_t peakReservedBytes = 0;
    uint32_t peakPagesUsed = 0;
};

/// 只负责计算 page/offset，不创建或持有后端 GPU 资源。
/// beginRecording() 必须在确认旧录制使用的 GPU page 已可安全复用后调用。
class TransientUniformAllocator final {
public:
    explicit TransientUniformAllocator(UniformAllocatorConfig config);

    bool isValid() const noexcept { return valid_; }
    const UniformAllocatorConfig& config() const noexcept { return config_; }

    uint64_t beginRecording() noexcept;
    std::expected<UniformAllocationPlan, UniformAllocationError> allocate(uint32_t size) noexcept;

    uint64_t recordingGeneration() const noexcept { return recording_generation_; }
    bool owns(uint64_t generation) const noexcept { return generation != 0 && generation == recording_generation_; }

    const UniformAllocatorStats& stats() const noexcept { return stats_; }

private:
    static bool alignUp(uint32_t value, uint32_t alignment, uint32_t& aligned) noexcept;

    UniformAllocatorConfig config_;
    UniformAllocatorStats stats_;
    uint64_t recording_generation_ = 0;
    uint32_t active_page_ = 0;
    uint32_t cursor_ = 0;
    bool valid_ = false;
    bool recording_ = false;
};

}  // namespace mulan::engine
