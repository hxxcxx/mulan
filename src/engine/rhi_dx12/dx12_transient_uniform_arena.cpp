#include "detail/dx12_transient_uniform_arena.h"

#include "detail/dx12_buffer.h"

#include <cstring>

namespace mulan::engine {

DX12TransientUniformArena::DX12TransientUniformArena(ID3D12Device* device) : device_(device) {
}

DX12TransientUniformArena::~DX12TransientUniformArena() = default;

void DX12TransientUniformArena::beginRecording() {
    allocator_.beginRecording();
}

void DX12TransientUniformArena::endRecording() {
    allocator_.endRecording();
}

DX12TransientUniformArena::Allocation DX12TransientUniformArena::upload(std::span<const std::byte> data) {
    if (!device_ || data.empty())
        return {};

    const auto plan = allocator_.allocate(static_cast<uint32_t>(data.size_bytes()));
    if (!plan)
        return {};

    DX12Buffer* page = acquirePage(plan->pageIndex);
    if (!page || !page->mappedData())
        return {};

    auto* destination = static_cast<std::byte*>(page->mappedData()) + plan->offset;
    std::memset(destination, 0, plan->reservedSize);
    std::memcpy(destination, data.data(), data.size_bytes());
    return { page, plan->offset, plan->size, plan->recordingGeneration };
}

DX12Buffer* DX12TransientUniformArena::acquirePage(uint32_t pageIndex) {
    while (pages_.size() <= pageIndex) {
        auto result = DX12Buffer::create(BufferDesc::uniform(kPageBytes, "DX12TransientUniformPage"), device_);
        if (!result)
            return nullptr;
        pages_.push_back(std::move(*result));
    }
    return pages_[pageIndex].get();
}

}  // namespace mulan::engine
