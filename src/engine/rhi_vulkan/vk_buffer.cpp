#include "detail/vk_buffer.h"
#include "detail/vk_debug_name.h"

#include <mulan/core/result/error.h>
#include "../rhi/engine_error_code.h"

#include <cstring>

namespace mulan::engine {

Result<std::unique_ptr<VKBuffer>> VKBuffer::create(const BufferDesc& desc, VmaAllocator allocator) {
    auto obj = std::unique_ptr<VKBuffer>(new VKBuffer(desc));
    obj->allocator_ = allocator;

    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = desc.size;
    ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    switch (desc.usage) {
    case BufferUsage::Immutable:
        // 始终分配设备本地内存，通过 staging buffer 上传初始数据
        // 避免在 render pass 内部做 host mapping 导致驱动问题
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case BufferUsage::Default: allocInfo.usage = VMA_MEMORY_USAGE_AUTO; break;
    case BufferUsage::Dynamic:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;
    case BufferUsage::Staging:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        ci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocResult;

    VkResult res = vmaCreateBuffer(allocator, &ci, &allocInfo, &buffer, &allocation, &allocResult);
    if (res != VK_SUCCESS) {
        return std::unexpected(makeError(EngineErrorCode::BufferCreateFailed,
                                         "vmaCreateBuffer failed: VkResult=" + std::to_string(res)));
    }

    obj->buffer_ = vk::Buffer(buffer);
    obj->allocation_ = allocation;
    obj->mapped_data_ = allocResult.pMappedData;

    // 上传初始数据
    if (desc.initData && obj->mapped_data_) {
        std::memcpy(obj->mapped_data_, desc.initData, desc.size);
        vmaFlushAllocation(allocator, allocation, 0, desc.size);
    } else if (desc.initData) {
        // 不可映射的 immutable buffer 需要暂存缓冲区（后续由 VKDevice 处理）
        // 拷贝数据到自有存储，避免悬挂指针
        obj->pending_data_.resize(desc.size);
        std::memcpy(obj->pending_data_.data(), desc.initData, desc.size);
    }

    obj->desc_.discardInitialData();
    return obj;
}

VKBuffer::~VKBuffer() {
    waitForLastUseBeforeDestruction();
    if (buffer_ && allocator_) {
        vmaDestroyBuffer(allocator_, VkBuffer(buffer_), allocation_);
    }
}

Result<void> VKBuffer::write(uint32_t offset, uint32_t size, const void* data) {
    if (auto wait = waitForLastUse(); !wait)
        return std::unexpected(wait.error());
    if (desc_.usage != BufferUsage::Dynamic || !mapped_data_ || !data || size == 0 || offset > desc_.size ||
        size > desc_.size - offset) {
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed,
                                         "Vulkan buffer write requires a valid Dynamic buffer range"));
    }
    memcpy(static_cast<uint8_t*>(mapped_data_) + offset, data, size);
    if (vmaFlushAllocation(allocator_, allocation_, offset, size) != VK_SUCCESS) {
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan buffer memory flush failed"));
    }
    return {};
}

Result<void> VKBuffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (auto wait = waitForLastUse(); !wait)
        return std::unexpected(wait.error());
    if (desc_.usage != BufferUsage::Staging || !mapped_data_ || !outData || offset > desc_.size ||
        size > desc_.size - offset) {
        return std::unexpected(makeError(EngineErrorCode::ResourceReadbackFailed,
                                         "Vulkan buffer readback requires a valid mapped staging range"));
    }

    // 确保 GPU 写入对 CPU 可见
    vmaInvalidateAllocation(allocator_, allocation_, offset, size);
    memcpy(outData, static_cast<const uint8_t*>(mapped_data_) + offset, size);
    return {};
}

}  // namespace mulan::engine
