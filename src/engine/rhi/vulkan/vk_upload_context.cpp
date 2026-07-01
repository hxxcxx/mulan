#include "vk_upload_context.h"
#include "vk_buffer.h"

namespace mulan::engine {

VKUploadContext::VKUploadContext(vk::Device device, VmaAllocator allocator,
                                 uint32_t queueFamily, vk::Queue queue)
    : device_(device)
    , allocator_(allocator)
    , queue_family_(queueFamily)
    , queue_(queue)
{
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags            = vk::CommandPoolCreateFlagBits::eTransient;
    poolCI.queueFamilyIndex = queue_family_;
    cmd_pool_ = device_.createCommandPool(poolCI);

    upload_fence_ = device_.createFence({});
}

VKUploadContext::~VKUploadContext() {
    flush();

    for (auto& slab : slabs_) {
        vmaDestroyBuffer(allocator_, VkBuffer(slab.buffer), slab.allocation);
    }
    slabs_.clear();

    if (upload_fence_) device_.destroyFence(upload_fence_);
    if (cmd_pool_)     device_.destroyCommandPool(cmd_pool_);
}

void VKUploadContext::uploadToBuffer(VKBuffer* dst, const void* data, uint32_t size,
                                      uint32_t dstOffset) {
    auto slice = allocStaging(size);
    memcpy(slice.mapped, data, size);

    vmaFlushAllocation(allocator_, slice.allocation, slice.offset, size);

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
    std::lock_guard lock(mutex_);

    for (auto& slab : slabs_) {
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
    slabs_.push_back(slab);

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
    std::lock_guard lock(mutex_);
    for (auto& slab : slabs_) {
        slab.used = 0;
    }
}

void VKUploadContext::flush() {
    if (pending_) {
        device_.waitForFences(upload_fence_, true, UINT64_MAX);
        device_.resetFences(upload_fence_);
        pending_ = false;
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
    vmaCreateBuffer(allocator_, &ci, &allocCI, &buffer, &allocation, &info);

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
