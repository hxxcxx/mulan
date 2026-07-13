#include "detail/dx11_constant_buffer_arena.h"

#include <algorithm>
#include <cstring>

namespace mulan::engine {
namespace {

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

DX11ConstantBufferArena::DX11ConstantBufferArena(ID3D11Device* device, ID3D11DeviceContext* context,
                                                 ID3D11DeviceContext1* context1)
    : device_(device), context_(context), context1_(context1) {
    if (!device_ || !context_)
        return;

    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    linear_suballocation_ =
            context1_ &&
            SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) &&
            options.MapNoOverwriteOnDynamicConstantBuffer;
    LOG_INFO("[DX11] Constant buffer arena initialized: pageBytes={}, linearSuballocation={}", kPageBytes,
             linear_suballocation_);
}

void DX11ConstantBufferArena::beginFrame() {
    active_page_ = 0;
    for (auto& page : pages_) {
        page.cursor = 0;
        page.discarded = false;
    }
}

bool DX11ConstantBufferArena::createPage(Page& page) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = kPageBytes;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const auto result = checkDX11(device_->CreateBuffer(&desc, nullptr, &page.buffer),
                                  "ID3D11Device::CreateBuffer(constant buffer arena)");
    return static_cast<bool>(result);
}

DX11ConstantBufferArena::Page* DX11ConstantBufferArena::acquirePage(uint32_t bytes) {
    if (!linear_suballocation_) {
        if (pages_.empty()) {
            pages_.emplace_back();
            if (!createPage(pages_.back())) {
                pages_.clear();
                return nullptr;
            }
        }
        return &pages_.front();
    }

    while (active_page_ < pages_.size() && pages_[active_page_].cursor + bytes > kPageBytes)
        ++active_page_;
    if (active_page_ == pages_.size()) {
        pages_.emplace_back();
        if (!createPage(pages_.back())) {
            pages_.pop_back();
            return nullptr;
        }
    }
    return &pages_[active_page_];
}

DX11ConstantBufferArena::Allocation DX11ConstantBufferArena::upload(const void* data, uint32_t size) {
    if (!data || size == 0 || size > kPageBytes || !isValid())
        return {};

    const uint32_t allocationBytes = alignUp(size, kAllocationAlignment);
    Page* page = acquirePage(allocationBytes);
    if (!page)
        return {};

    const uint32_t offset = linear_suballocation_ ? page->cursor : 0;
    const D3D11_MAP mapMode =
            linear_suballocation_ && page->discarded ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = context_->Map(page->buffer.Get(), 0, mapMode, 0, &mapped);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11DeviceContext::Map(constant buffer arena)");
        return {};
    }

    auto* destination = static_cast<uint8_t*>(mapped.pData) + offset;
    std::memset(destination, 0, allocationBytes);
    std::memcpy(destination, data, size);
    context_->Unmap(page->buffer.Get(), 0);

    page->discarded = true;
    if (linear_suballocation_)
        page->cursor += allocationBytes;

    Allocation allocation;
    allocation.buffer = page->buffer.Get();
    allocation.firstConstant = offset / 16u;
    allocation.constantCount = allocationBytes / 16u;
    allocation.ranged = linear_suballocation_;
    return allocation;
}

}  // namespace mulan::engine
