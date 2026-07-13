#include "transient_uniform_allocator.h"

#include <algorithm>
#include <limits>

namespace mulan::engine {

TransientUniformAllocator::TransientUniformAllocator(UniformAllocatorConfig config) : config_(config) {
    valid_ = config_.pageSize != 0 && config_.alignment != 0 && config_.alignment <= config_.pageSize &&
             config_.maxAllocationSize != 0 && config_.maxAllocationSize <= config_.pageSize;
}

uint64_t TransientUniformAllocator::beginRecording() noexcept {
    ++recording_generation_;
    if (recording_generation_ == 0)
        ++recording_generation_;

    active_page_ = 0;
    cursor_ = 0;
    recording_ = valid_;
    stats_.allocationCount = 0;
    stats_.requestedBytes = 0;
    stats_.reservedBytes = 0;
    stats_.pagesUsed = 0;
    return recording_generation_;
}

std::expected<UniformAllocationPlan, UniformAllocationError> TransientUniformAllocator::allocate(
        uint32_t size) noexcept {
    if (!valid_)
        return std::unexpected(UniformAllocationError::InvalidConfiguration);
    if (!recording_)
        return std::unexpected(UniformAllocationError::RecordingNotStarted);
    if (size == 0)
        return std::unexpected(UniformAllocationError::EmptyAllocation);
    if (size > config_.maxAllocationSize)
        return std::unexpected(UniformAllocationError::AllocationTooLarge);

    uint32_t reservedSize = 0;
    if (!alignUp(size, config_.alignment, reservedSize))
        return std::unexpected(UniformAllocationError::ArithmeticOverflow);
    if (reservedSize > config_.pageSize)
        return std::unexpected(UniformAllocationError::AllocationTooLarge);

    uint32_t offset = 0;
    if (!alignUp(cursor_, config_.alignment, offset))
        return std::unexpected(UniformAllocationError::ArithmeticOverflow);

    if (offset > config_.pageSize || reservedSize > config_.pageSize - offset) {
        if (active_page_ == std::numeric_limits<uint32_t>::max())
            return std::unexpected(UniformAllocationError::ArithmeticOverflow);
        ++active_page_;
        offset = 0;
    }

    cursor_ = offset + reservedSize;
    ++stats_.allocationCount;
    stats_.requestedBytes += size;
    stats_.reservedBytes += reservedSize;
    stats_.pagesUsed = (std::max) (stats_.pagesUsed, active_page_ + 1);
    stats_.peakReservedBytes = (std::max) (stats_.peakReservedBytes, stats_.reservedBytes);
    stats_.peakPagesUsed = (std::max) (stats_.peakPagesUsed, stats_.pagesUsed);

    return UniformAllocationPlan{ active_page_, offset, size, reservedSize, recording_generation_ };
}

bool TransientUniformAllocator::alignUp(uint32_t value, uint32_t alignment, uint32_t& aligned) noexcept {
    const uint32_t remainder = value % alignment;
    if (remainder == 0) {
        aligned = value;
        return true;
    }

    const uint32_t padding = alignment - remainder;
    if (value > std::numeric_limits<uint32_t>::max() - padding)
        return false;
    aligned = value + padding;
    return true;
}

}  // namespace mulan::engine
