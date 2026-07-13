#include "detail/gl_transient_uniform_arena.h"

#include "detail/gl_buffer.h"

#include <algorithm>
#include <cstring>

namespace mulan::engine {
namespace {

UniformAllocatorConfig makeAllocatorConfig(uint32_t alignment, uint32_t maxAllocationSize) {
    constexpr uint32_t kPreferredPageBytes = 256 * 1024;
    const uint32_t resolvedAlignment = (std::max) (1u, alignment);
    const uint32_t resolvedMaximum = (std::max) (1u, maxAllocationSize);
    return { (std::max) ({ kPreferredPageBytes, resolvedAlignment, resolvedMaximum }), resolvedAlignment,
             resolvedMaximum };
}

}  // namespace

GLTransientUniformArena::GLTransientUniformArena(uint32_t alignment, uint32_t maxAllocationSize)
    : allocator_(makeAllocatorConfig(alignment, maxAllocationSize)) {
}

GLTransientUniformArena::~GLTransientUniformArena() {
    for (auto& batch : batches_) {
        if (batch.fence)
            glDeleteSync(batch.fence);
    }
}

bool GLTransientUniformArena::beginRecording() {
    if (active_batch_ != kInvalidBatch)
        sealRecording();

    for (uint32_t i = 0; i < batches_.size(); ++i) {
        const uint32_t index = (reuse_cursor_ + i) % static_cast<uint32_t>(batches_.size());
        if (releaseCompletedFence(batches_[index])) {
            active_batch_ = index;
            reuse_cursor_ = (index + 1) % kMaximumBatches;
            allocator_.beginRecording();
            return true;
        }
    }

    if (batches_.size() < kMaximumBatches) {
        batches_.emplace_back();
        active_batch_ = static_cast<uint32_t>(batches_.size() - 1);
        reuse_cursor_ = (active_batch_ + 1) % kMaximumBatches;
        allocator_.beginRecording();
        return true;
    }

    const uint32_t index = reuse_cursor_ % static_cast<uint32_t>(batches_.size());
    if (!waitForFence(batches_[index]))
        return false;
    active_batch_ = index;
    reuse_cursor_ = (index + 1) % kMaximumBatches;
    allocator_.beginRecording();
    return true;
}

void GLTransientUniformArena::sealRecording() {
    if (active_batch_ == kInvalidBatch)
        return;
    Batch& batch = batches_[active_batch_];
    if (batch.fence)
        glDeleteSync(batch.fence);
    batch.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!batch.fence) {
        LOG_ERROR("[OpenGL] Transient uniform batch fence creation failed");
        glFinish();
    }
    active_batch_ = kInvalidBatch;
}

GLTransientUniformArena::Allocation GLTransientUniformArena::upload(std::span<const std::byte> data) {
    if (active_batch_ == kInvalidBatch || data.empty())
        return {};
    const auto plan = allocator_.allocate(static_cast<uint32_t>(data.size_bytes()));
    if (!plan)
        return {};
    GLBuffer* page = acquirePage(plan->pageIndex);
    if (!page || !page->mappedData())
        return {};

    auto* destination = static_cast<std::byte*>(page->mappedData()) + plan->offset;
    std::memset(destination, 0, plan->reservedSize);
    std::memcpy(destination, data.data(), data.size_bytes());
    return { page, plan->offset, plan->size, plan->recordingGeneration };
}

bool GLTransientUniformArena::releaseCompletedFence(Batch& batch) {
    if (!batch.fence)
        return true;
    const GLenum result = glClientWaitSync(batch.fence, 0, 0);
    if (result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED)
        return false;
    glDeleteSync(batch.fence);
    batch.fence = nullptr;
    return true;
}

bool GLTransientUniformArena::waitForFence(Batch& batch) {
    if (!batch.fence)
        return true;
    for (;;) {
        const GLenum result = glClientWaitSync(batch.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
            glDeleteSync(batch.fence);
            batch.fence = nullptr;
            return true;
        }
        if (result == GL_WAIT_FAILED)
            return false;
    }
}

GLBuffer* GLTransientUniformArena::acquirePage(uint32_t pageIndex) {
    Batch& batch = batches_[active_batch_];
    while (batch.pages.size() <= pageIndex) {
        auto page = GLBuffer::createTransientUniformPage(allocator_.config().pageSize);
        if (!page)
            return nullptr;
        batch.pages.push_back(std::move(page));
    }
    return batch.pages[pageIndex].get();
}

}  // namespace mulan::engine
