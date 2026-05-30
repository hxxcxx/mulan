#include "VKUploadContext.h"
#include "VKBuffer.h"

namespace mulan::engine {

VKUploadContext::VKUploadContext(vk::Device device, VmaAllocator allocator,
                                 uint32_t queueFamily, vk::Queue queue)
    : m_device(device)
    , m_allocator(allocator)
    , m_queueFamily(queueFamily)
    , m_queue(queue)
{
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags            = vk::CommandPoolCreateFlagBits::eTransient;
    poolCI.queueFamilyIndex = m_queueFamily;
    m_cmdPool = m_device.createCommandPool(poolCI);

    m_uploadFence = m_device.createFence({});
}

VKUploadContext::~VKUploadContext() {
    flush();

    for (auto& slab : m_slabs) {
        vmaDestroyBuffer(m_allocator, VkBuffer(slab.buffer), slab.allocation);
    }
    m_slabs.clear();

    if (m_uploadFence) m_device.destroyFence(m_uploadFence);
    if (m_cmdPool)     m_device.destroyCommandPool(m_cmdPool);
}

void VKUploadContext::uploadToBuffer(VKBuffer* dst, const void* data, uint32_t size,
                                      uint32_t dstOffset) {
    auto slice = allocStaging(size);
    memcpy(slice.mapped, data, size);

    vmaFlushAllocation(m_allocator, slice.allocation, slice.offset, size);

    executeCopy(
        [&](vk::CommandBuffer cmd) {
            vk::BufferCopy region;
            region.srcOffset = slice.offset;
            region.dstOffset = dstOffset;
            region.size      = size;
            cmd.copyBuffer(slice.buffer, dst->vkBuffer(), 1, &region);
        }
    );
}

void VKUploadContext::uploadBufferInit(VKBuffer* dst) {
    uploadToBuffer(dst, dst->pendingData(), dst->desc().size);
    dst->markUploaded();
}

StagingSlice VKUploadContext::allocStaging(uint32_t size) {
    std::lock_guard lock(m_mutex);

    for (auto& slab : m_slabs) {
        if (slab.used + size <= slab.capacity) {
            StagingSlice slice;
            slice.buffer     = slab.buffer;
            slice.allocation = slab.allocation;
            slice.mapped     = slab.mapped;
            slice.offset     = slab.used;
            slice.size       = size;
            slab.used       += size;
            return slice;
        }
    }

    uint32_t slabSize = (std::max)(uint32_t(4 * 1024 * 1024), alignUp(size, 256));
    auto slab = createSlab(slabSize);
    m_slabs.push_back(slab);

    StagingSlice slice;
    slice.buffer     = slab.buffer;
    slice.allocation = slab.allocation;
    slice.mapped     = slab.mapped;
    slice.offset     = 0;
    slice.size       = size;
    slab.used        = size;
    return slice;
}

void VKUploadContext::resetSlabs() {
    std::lock_guard lock(m_mutex);
    for (auto& slab : m_slabs) {
        slab.used = 0;
    }
}

void VKUploadContext::flush() {
    if (m_pending) {
        m_device.waitForFences(m_uploadFence, true, UINT64_MAX);
        m_device.resetFences(m_uploadFence);
        m_pending = false;
        resetSlabs();
    }
}

VKUploadContext::Slab VKUploadContext::createSlab(uint32_t size) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size  = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                  | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
    vmaCreateBuffer(m_allocator, &ci, &allocCI, &buffer, &allocation, &info);

    Slab slab;
    slab.buffer     = vk::Buffer(buffer);
    slab.allocation = allocation;
    slab.mapped     = info.pMappedData;
    slab.capacity   = size;
    slab.used       = 0;
    return slab;
}

uint32_t VKUploadContext::alignUp(uint32_t v, uint32_t align) {
    return (v + align - 1) & ~(align - 1);
}

} // namespace mulan::engine
