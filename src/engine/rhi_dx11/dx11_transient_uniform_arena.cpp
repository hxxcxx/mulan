#include "detail/dx11_transient_uniform_arena.h"
#include "detail/dx11_buffer.h"

#include <algorithm>
#include <cstring>

namespace mulan::engine {
namespace {

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

DX11TransientUniformArena::DX11TransientUniformArena(ID3D11Device* device, ID3D11DeviceContext* context,
                                                     ID3D11DeviceContext1* context1)
    : device_(device), context_(context), context1_(context1) {
    if (!device_ || !context_)
        return;

    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    linear_suballocation_ =
            context1_ &&
            SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) &&
            options.MapNoOverwriteOnDynamicConstantBuffer;
    LOG_INFO("[DX11] Transient uniform arena initialized: pageBytes={}, linearSuballocation={}", kPageBytes,
             linear_suballocation_);
}

void DX11TransientUniformArena::beginRecording() {
    allocator_.beginRecording();
    fallback_page_ = 0;
    for (auto& page : pages_) {
        page.discarded = false;
    }
}

bool DX11TransientUniformArena::createPage(Page& page, uint32_t capacity) {
    page.buffer = DX11Buffer::createTransientUniformPage(capacity, device_, context_);
    page.capacity = page.buffer ? capacity : 0;
    return page.buffer != nullptr;
}

DX11TransientUniformArena::Page* DX11TransientUniformArena::acquireLinearPage(uint32_t pageIndex) {
    while (pages_.size() <= pageIndex) {
        pages_.emplace_back();
        if (!createPage(pages_.back(), kPageBytes)) {
            pages_.pop_back();
            return nullptr;
        }
    }
    return &pages_[pageIndex];
}

DX11TransientUniformArena::Page* DX11TransientUniformArena::acquireFallbackPage(uint32_t pageIndex, uint32_t capacity) {
    while (pages_.size() <= pageIndex)
        pages_.emplace_back();
    Page& page = pages_[pageIndex];
    if (!page.buffer || page.capacity < capacity) {
        if (page.buffer)
            retired_pages_.push_back(std::move(page.buffer));
        page = {};
        if (!createPage(page, capacity))
            return nullptr;
    }
    return &page;
}

DX11TransientUniformArena::Allocation DX11TransientUniformArena::upload(std::span<const std::byte> data) {
    if (data.empty() || data.size_bytes() > kPageBytes || !isValid())
        return {};

    const uint32_t size = static_cast<uint32_t>(data.size_bytes());

    const auto plan = allocator_.allocate(size);
    if (!plan)
        return {};

    const uint32_t allocationBytes = plan->reservedSize;
    const uint32_t pageIndex = linear_suballocation_ ? plan->pageIndex : fallback_page_++;
    Page* page = linear_suballocation_ ? acquireLinearPage(pageIndex) : acquireFallbackPage(pageIndex, allocationBytes);
    if (!page)
        return {};

    const uint32_t offset = linear_suballocation_ ? plan->offset : 0;
    const D3D11_MAP mapMode =
            linear_suballocation_ && page->discarded ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = context_->Map(page->buffer->buffer(), 0, mapMode, 0, &mapped);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11DeviceContext::Map(constant buffer arena)");
        return {};
    }

    auto* destination = static_cast<uint8_t*>(mapped.pData) + offset;
    std::memset(destination, 0, allocationBytes);
    std::memcpy(destination, data.data(), size);
    context_->Unmap(page->buffer->buffer(), 0);

    page->discarded = true;

    Allocation allocation;
    allocation.buffer = page->buffer->buffer();
    allocation.firstConstant = offset / 16u;
    allocation.constantCount = allocationBytes / 16u;
    allocation.ranged = linear_suballocation_;
    allocation.backingBuffer = page->buffer.get();
    return allocation;
}

}  // namespace mulan::engine
