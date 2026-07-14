/**
 * @file dx11_transient_uniform_arena.h
 * @brief D3D11 瞬态 Uniform 页分配器
 * @author hxxcxx
 * @date 2026-07-14
 */

#pragma once

#include "dx11_common.h"
#include "../../rhi/transient_uniform_allocator.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace mulan::engine {

class DX11Buffer;

class DX11TransientUniformArena final {
public:
    struct Allocation {
        ID3D11Buffer* buffer = nullptr;
        UINT firstConstant = 0;
        UINT constantCount = 0;
        bool ranged = false;
        DX11Buffer* backingBuffer = nullptr;

        explicit operator bool() const { return buffer != nullptr; }
    };

    DX11TransientUniformArena(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11DeviceContext1* context1);

    void beginRecording();
    void endRecording();
    Allocation upload(std::span<const std::byte> data);

    bool isValid() const { return device_ && context_; }
    bool usesLinearSuballocation() const { return linear_suballocation_; }
    uint64_t recordingGeneration() const { return allocator_.recordingGeneration(); }

private:
    static constexpr uint32_t kPageBytes = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
    static constexpr uint32_t kAllocationAlignment = 256;

    struct Page {
        std::unique_ptr<DX11Buffer> buffer;
        uint32_t capacity = 0;
        bool discarded = false;
    };

    bool createPage(Page& page, uint32_t capacity);
    Page* acquireLinearPage(uint32_t pageIndex);
    Page* acquireFallbackPage(uint32_t pageIndex, uint32_t capacity);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11DeviceContext1* context1_ = nullptr;
    bool linear_suballocation_ = false;
    TransientUniformAllocator allocator_{ { kPageBytes, kAllocationAlignment, kPageBytes } };
    uint32_t fallback_page_ = 0;
    std::vector<Page> pages_;
    std::vector<std::unique_ptr<DX11Buffer>> retired_pages_;
};

}  // namespace mulan::engine
