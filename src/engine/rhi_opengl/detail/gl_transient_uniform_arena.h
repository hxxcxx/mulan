/**
 * @file gl_transient_uniform_arena.h
 * @brief OpenGL 瞬态 Uniform 持久映射页分配器
 * @author hxxcxx
 * @date 2026-07-13
 */

#pragma once

#include "../../rhi/transient_uniform_allocator.h"
#include "gl_common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mulan::engine {

class GLBuffer;

class GLTransientUniformArena final {
public:
    struct Allocation {
        GLBuffer* backingBuffer = nullptr;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint64_t recordingGeneration = 0;

        explicit operator bool() const { return backingBuffer != nullptr; }
    };

    GLTransientUniformArena(uint32_t alignment, uint32_t maxAllocationSize);
    ~GLTransientUniformArena();

    bool beginRecording();
    void endRecording();
    Allocation upload(std::span<const std::byte> data);

    uint32_t alignment() const { return allocator_.config().alignment; }
    uint32_t maxAllocationSize() const { return allocator_.config().maxAllocationSize; }
    uint64_t recordingGeneration() const { return allocator_.recordingGeneration(); }

private:
    static constexpr uint32_t kMaximumBatches = 3;
    static constexpr uint32_t kInvalidBatch = UINT32_MAX;

    struct Batch {
        std::vector<std::unique_ptr<GLBuffer>> pages;
        GLsync fence = nullptr;
    };

    bool releaseCompletedFence(Batch& batch);
    bool waitForFence(Batch& batch);
    GLBuffer* acquirePage(uint32_t pageIndex);

    TransientUniformAllocator allocator_;
    std::vector<Batch> batches_;
    uint32_t active_batch_ = kInvalidBatch;
    uint32_t reuse_cursor_ = 0;
};

}  // namespace mulan::engine
