/**
 * @file vk_buffer.h
 * @brief Vulkan缓冲区实现
 * @author hxxcxx
 * @date 2026-04-15
 */

#pragma once

#include "../buffer.h"
#include "vk_convert.h"

#include <vector>

namespace mulan::engine {

class VKBuffer : public Buffer {
public:
    VKBuffer(const BufferDesc& desc, VmaAllocator allocator);
    ~VKBuffer();

    const BufferDesc& desc() const override { return desc_; }

    vk::Buffer vkBuffer() const { return buffer_; }
    VmaAllocation allocation() const { return allocation_; }
    void* mappedData() const { return mapped_data_; }
    const void* pendingData() const { return pending_data_.data(); }
    bool needsUpload() const { return !pending_data_.empty(); }
    /// 标记初始数据已同步上传完成（仅由 VKUploadContext 调用）
    void markUploaded() { pending_data_.clear(); pending_data_.shrink_to_fit(); }

    void update(uint32_t offset, uint32_t size, const void* data) override;
    bool readback(uint32_t offset, uint32_t size, void* outData) override;

private:
    BufferDesc      desc_;
    VmaAllocator    allocator_ = nullptr;
    vk::Buffer      buffer_;
    VmaAllocation   allocation_ = nullptr;
    void*           mapped_data_  = nullptr;
    std::vector<uint8_t> pending_data_;
};

} // namespace mulan::engine
