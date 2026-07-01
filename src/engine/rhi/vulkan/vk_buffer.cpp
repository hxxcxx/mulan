#include "vk_buffer.h"

namespace mulan::engine {

VKBuffer::VKBuffer(const BufferDesc& desc, VmaAllocator allocator)
    : desc_(desc), allocator_(allocator)
{
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size  = desc.size;
    ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
             | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
             | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
             | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    switch (desc.usage) {
        case BufferUsage::Immutable:
            // 始终分配设备本地内存，通过 staging buffer 上传初始数据
            // 避免在 render pass 内部做 host mapping 导致驱动问题
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case BufferUsage::Default:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            break;
        case BufferUsage::Dynamic:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                           | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            break;
        case BufferUsage::Staging:
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                           | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            ci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                      | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
    }

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocResult;

    VkResult res = vmaCreateBuffer(allocator, &ci, &allocInfo,
                                   &buffer, &allocation, &allocResult);
    if (res != VK_SUCCESS) return;

    buffer_     = vk::Buffer(buffer);
    allocation_ = allocation;
    mapped_data_ = allocResult.pMappedData;

    // 上传初始数据
    if (desc.initData && mapped_data_) {
        memcpy(mapped_data_, desc.initData, desc.size);
        vmaFlushAllocation(allocator, allocation_, 0, desc.size);
    } else if (desc.initData) {
        // 不可映射的 immutable buffer 需要暂存缓冲区（后续由 VKDevice 处理）
        // 拷贝数据到自有存储，避免悬挂指针
        pending_data_.resize(desc.size);
        memcpy(pending_data_.data(), desc.initData, desc.size);
    }
}

VKBuffer::~VKBuffer() {
    if (buffer_ && allocator_) {
        vmaDestroyBuffer(allocator_, VkBuffer(buffer_), allocation_);
    }
}

void VKBuffer::update(uint32_t offset, uint32_t size, const void* data) {
    if (mapped_data_) {
        memcpy(static_cast<uint8_t*>(mapped_data_) + offset, data, size);
        vmaFlushAllocation(allocator_, allocation_, offset, size);
    } else {
        // Immutable / device-local buffer: update() 不适用
        // 初始数据通过 pending_data_ + uploadBufferInit() 同步上传
        // 运行时更新应使用 cmd->updateBuffer() (GPU-side copy)
    }
}

bool VKBuffer::readback(uint32_t offset, uint32_t size, void* outData) {
    if (desc_.usage != BufferUsage::Staging || !mapped_data_) return false;

    // 确保 GPU 写入对 CPU 可见
    vmaInvalidateAllocation(allocator_, allocation_, offset, size);
    memcpy(outData, static_cast<const uint8_t*>(mapped_data_) + offset, size);
    return true;
}

} // namespace mulan::engine
