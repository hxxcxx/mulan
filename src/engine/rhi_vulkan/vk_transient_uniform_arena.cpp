#include "detail/vk_transient_uniform_arena.h"

#include "detail/vk_buffer.h"

#include <algorithm>

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

VKTransientUniformArena::VKTransientUniformArena(VmaAllocator allocator, uint32_t alignment, uint32_t maxAllocationSize)
    : allocator_(allocator), allocator_plan_(makeAllocatorConfig(alignment, maxAllocationSize)) {
}

VKTransientUniformArena::~VKTransientUniformArena() = default;

void VKTransientUniformArena::beginRecording() {
    allocator_plan_.beginRecording();
}

void VKTransientUniformArena::endRecording() {
    allocator_plan_.endRecording();
}

VKTransientUniformArena::Allocation VKTransientUniformArena::upload(std::span<const std::byte> data) {
    if (!allocator_ || data.empty())
        return {};
    const auto plan = allocator_plan_.allocate(static_cast<uint32_t>(data.size_bytes()));
    if (!plan)
        return {};
    VKBuffer* page = acquirePage(plan->pageIndex);
    if (!page || !page->mappedData())
        return {};

    page->update(plan->offset, plan->size, data.data());
    return { page, plan->offset, plan->size, plan->recordingGeneration };
}

VKBuffer* VKTransientUniformArena::acquirePage(uint32_t pageIndex) {
    while (pages_.size() <= pageIndex) {
        auto result = VKBuffer::create(BufferDesc::uniform(allocator_plan_.config().pageSize, "VKTransientUniformPage"),
                                       allocator_);
        if (!result)
            return nullptr;
        pages_.push_back(std::move(*result));
    }
    return pages_[pageIndex].get();
}

}  // namespace mulan::engine
