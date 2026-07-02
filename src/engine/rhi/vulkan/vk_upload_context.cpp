#include "vk_upload_context.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "vk_convert.h"

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

void VKUploadContext::uploadTexture(VKTexture* dst, const void* data,
                                    uint32_t width, uint32_t height,
                                    TextureFormat format) {
    const uint32_t bpp = textureFormatBytesPerPixel(format);
    if (bpp == 0 || width == 0 || height == 0) return;

    // Vulkan 要求 bufferRowLength 对齐到 4 字节；颜色格式 bpp 通常 ≥ 4，
    // 但 R8 需显式对齐。staging 内的行距 = 对齐后的 bytesPerRow。
    const uint32_t rowSize   = width * bpp;
    const uint32_t rowAlign  = 4;
    const uint32_t rowStride = (rowSize + rowAlign - 1) & ~(rowAlign - 1);
    const uint32_t dataSize  = rowStride * height;

    auto slice = allocStaging(dataSize);
    if (rowStride == rowSize) {
        memcpy(slice.mapped, data, dataSize);
    } else {
        // 行距 > 行实际大小时逐行拷贝
        const auto* src = static_cast<const uint8_t*>(data);
        auto* dstPtr = static_cast<uint8_t*>(slice.mapped);
        for (uint32_t y = 0; y < height; ++y) {
            memcpy(dstPtr + y * rowStride, src + y * rowSize, rowSize);
        }
    }
    vmaFlushAllocation(allocator_, slice.allocation, slice.offset, dataSize);

    vk::Image image = dst->image();

    executeCopy(
        [&](vk::CommandBuffer cmd) {
            // eUndefined → eTransferDstOptimal
            vk::ImageMemoryBarrier b1;
            b1.srcAccessMask               = {};
            b1.dstAccessMask               = vk::AccessFlagBits::eTransferWrite;
            b1.oldLayout                   = vk::ImageLayout::eUndefined;
            b1.newLayout                   = vk::ImageLayout::eTransferDstOptimal;
            b1.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            b1.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            b1.image                       = image;
            b1.subresourceRange.aspectMask = VKTexture::isDepthFormat(format)
                ? vk::ImageAspectFlagBits::eDepth
                : vk::ImageAspectFlagBits::eColor;
            b1.subresourceRange.baseMipLevel   = 0;
            b1.subresourceRange.levelCount     = 1;
            b1.subresourceRange.baseArrayLayer = 0;
            b1.subresourceRange.layerCount     = 1;
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer,
                {}, {}, nullptr, b1);

            // buffer → image
            vk::BufferImageCopy region;
            region.bufferOffset      = slice.offset;
            region.bufferRowLength   = rowStride / bpp;  // 以 texel 为单位
            region.bufferImageHeight = height;
            region.imageSubresource.aspectMask = b1.subresourceRange.aspectMask;
            region.imageSubresource.mipLevel   = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;
            region.imageOffset = vk::Offset3D(0, 0, 0);
            region.imageExtent = vk::Extent3D(width, height, 1);
            cmd.copyBufferToImage(slice.buffer, image,
                                  vk::ImageLayout::eTransferDstOptimal, 1, &region);

            // eTransferDstOptimal → eShaderReadOnlyOptimal
            vk::ImageMemoryBarrier b2 = b1;
            b2.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            b2.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            b2.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
            b2.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader,
                {}, {}, nullptr, b2);
        }
    );

    dst->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
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
