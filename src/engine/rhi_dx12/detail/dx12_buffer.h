/**
 * @file dx12_buffer.h
 * @brief D3D12 缓冲区实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../rhi/buffer.h"
#include "dx12_common.h"

#include <mulan/core/result/error.h>

#include <expected>
#include <memory>
#include <vector>

namespace mulan::engine {

class DX12Buffer final : public Buffer {
public:
    /// 创建 DX12Buffer。失败返回 BufferCreateFailed。
    static core::Result<std::unique_ptr<DX12Buffer>> create(const BufferDesc& desc, ID3D12Device* device);
    ~DX12Buffer();

    const BufferDesc& desc() const override { return desc_; }
    void update(uint32_t offset, uint32_t size, const void* data) override;
    core::Result<void> readback(uint32_t offset, uint32_t size, void* outData) override;

    ID3D12Resource* resource() const { return resource_.Get(); }
    D3D12_RESOURCE_STATES state() const { return state_; }
    void setState(D3D12_RESOURCE_STATES state) { state_ = state; }
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return resource_->GetGPUVirtualAddress(); }
    void* mappedData() const { return mapped_data_; }
    uint64_t uploadFenceValue() const { return upload_fence_value_; }
    void setUploadFenceValue(uint64_t v) { upload_fence_value_ = v; }

    const void* pendingData() const { return pending_data_.data(); }
    bool needsUpload() const { return !pending_data_.empty(); }
    void markUploaded() {
        pending_data_.clear();
        pending_data_.shrink_to_fit();
    }

private:
    explicit DX12Buffer(const BufferDesc& desc) : desc_(desc) {}
    [[nodiscard]] core::Result<void> initialize(ID3D12Device* device);

    BufferDesc desc_;
    ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
    void* mapped_data_ = nullptr;
    uint64_t upload_fence_value_ = 0;
    std::vector<uint8_t> pending_data_;  // Immutable buffer 的待上传数据
};

}  // namespace mulan::engine
