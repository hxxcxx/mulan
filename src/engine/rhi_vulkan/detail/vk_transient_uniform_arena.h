/**
 * @file vk_transient_uniform_arena.h
 * @brief Vulkan 瞬态 Uniform 映射页分配器
 * @author hxxcxx
 * @date 2026-07-13
 */

#pragma once

#include "../../rhi/transient_uniform_allocator.h"
#include "vk_common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mulan::engine {

class VKBuffer;

class VKTransientUniformArena final {
public:
    struct Allocation {
        VKBuffer* backingBuffer = nullptr;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint64_t recordingGeneration = 0;

        explicit operator bool() const { return backingBuffer != nullptr; }
    };

    VKTransientUniformArena(VmaAllocator allocator, uint32_t alignment, uint32_t maxAllocationSize);
    ~VKTransientUniformArena();

    void beginRecording();
    void endRecording();
    Allocation upload(std::span<const std::byte> data);

    uint32_t alignment() const { return allocator_plan_.config().alignment; }
    uint32_t maxAllocationSize() const { return allocator_plan_.config().maxAllocationSize; }
    uint64_t recordingGeneration() const { return allocator_plan_.recordingGeneration(); }

private:
    VKBuffer* acquirePage(uint32_t pageIndex);

    VmaAllocator allocator_ = nullptr;
    TransientUniformAllocator allocator_plan_;
    std::vector<std::unique_ptr<VKBuffer>> pages_;
};

}  // namespace mulan::engine
