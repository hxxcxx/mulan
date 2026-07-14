/**
 * @file dx12_transient_uniform_arena.h
 * @brief D3D12 瞬态 Uniform 上传页分配器
 * @author hxxcxx
 * @date 2026-07-13
 */

#pragma once

#include "../../rhi/transient_uniform_allocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

struct ID3D12Device;

namespace mulan::engine {

class DX12Buffer;

class DX12TransientUniformArena final {
public:
    struct Allocation {
        DX12Buffer* backingBuffer = nullptr;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint64_t recordingGeneration = 0;

        explicit operator bool() const { return backingBuffer != nullptr; }
    };

    explicit DX12TransientUniformArena(ID3D12Device* device);
    ~DX12TransientUniformArena();

    void beginRecording();
    void endRecording();
    Allocation upload(std::span<const std::byte> data);

    uint64_t recordingGeneration() const { return allocator_.recordingGeneration(); }

private:
    static constexpr uint32_t kPageBytes = 256 * 1024;
    static constexpr uint32_t kAllocationAlignment = 256;
    static constexpr uint32_t kMaximumAllocationBytes = 64 * 1024;

    DX12Buffer* acquirePage(uint32_t pageIndex);

    ID3D12Device* device_ = nullptr;
    TransientUniformAllocator allocator_{ { kPageBytes, kAllocationAlignment, kMaximumAllocationBytes } };
    std::vector<std::unique_ptr<DX12Buffer>> pages_;
};

}  // namespace mulan::engine
