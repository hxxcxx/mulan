#include "detail/vk_upload_context.h"
#include "detail/vk_buffer.h"
#include "detail/vk_texture.h"
#include "detail/vk_convert.h"
#include "../rhi/engine_error_code.h"

namespace mulan::engine {

VKUploadContext::VKUploadContext(vk::Device device, VmaAllocator allocator, uint32_t queueFamily, vk::Queue queue)
    : device_(device), allocator_(allocator), queue_family_(queueFamily), queue_(queue) {
    vk::CommandPoolCreateInfo poolCI;
    poolCI.flags = vk::CommandPoolCreateFlagBits::eTransient;
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

    if (upload_fence_)
        device_.destroyFence(upload_fence_);
    if (cmd_pool_)
        device_.destroyCommandPool(cmd_pool_);
}

ResultVoid VKUploadContext::uploadToBuffer(VKBuffer* dst, const void* data, uint32_t size, uint32_t dstOffset) {
    if (!dst || !dst->vkBuffer() || !data || size == 0 || static_cast<uint64_t>(dstOffset) + size > dst->desc().size)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan buffer upload arguments are invalid"));
    auto slice = allocStaging(size);
    if (!slice.buffer || !slice.allocation || !slice.mapped)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan buffer staging allocation failed"));
    memcpy(static_cast<uint8_t*>(slice.mapped) + slice.offset, data, size);

    if (vmaFlushAllocation(allocator_, slice.allocation, slice.offset, size) != VK_SUCCESS)
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan buffer staging flush failed"));

    auto copyResult = executeCopy([&](vk::CommandBuffer cmd) {
        vk::BufferCopy region;
        region.srcOffset = slice.offset;
        region.dstOffset = dstOffset;
        region.size = size;
        cmd.copyBuffer(slice.buffer, dst->vkBuffer(), 1, &region);
    });
    if (!copyResult)
        return std::unexpected(copyResult.error());
    return {};
}

ResultVoid VKUploadContext::uploadBufferInit(VKBuffer* dst) {
    if (auto result = uploadToBuffer(dst, dst->pendingData(), dst->desc().size); !result)
        return std::unexpected(result.error());
    dst->markUploaded();
    return {};
}

ResultVoid VKUploadContext::uploadTexture(VKTexture* dst, const TextureUploadDesc& upload) {
    const uint32_t bpp = textureFormatBytesPerPixel(upload.format);
    const uint32_t sourceRowPitch = upload.sourceRowPitch ? upload.sourceRowPitch : upload.width * bpp;
    const uint64_t tightRowPitch = static_cast<uint64_t>(upload.width) * bpp;
    const uint64_t dataSize64 = tightRowPitch * upload.height;
    if (!dst || upload.data.empty() || bpp == 0 || upload.width == 0 || upload.height == 0 ||
        sourceRowPitch < tightRowPitch ||
        upload.data.size_bytes() < static_cast<size_t>(sourceRowPitch) * upload.height || dataSize64 > UINT32_MAX)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan texture upload arguments are invalid"));

    const auto& destinationDesc = dst->desc();
    if (upload.mipLevel >= destinationDesc.mipLevels || upload.arrayLayer >= destinationDesc.arraySize ||
        upload.format != destinationDesc.format ||
        upload.width != std::max(1u, destinationDesc.width >> upload.mipLevel) ||
        upload.height != std::max(1u, destinationDesc.height >> upload.mipLevel))
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan texture upload subresource is invalid"));

    const uint32_t rowSize = static_cast<uint32_t>(tightRowPitch);
    const uint32_t dataSize = static_cast<uint32_t>(dataSize64);

    auto slice = allocStaging(dataSize);
    if (!slice.buffer || !slice.allocation || !slice.mapped)
        return std::unexpected(
                makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan texture staging allocation failed"));
    auto* dstPtr = static_cast<uint8_t*>(slice.mapped) + slice.offset;
    if (sourceRowPitch == rowSize) {
        memcpy(dstPtr, upload.data.data(), dataSize);
    } else {
        const auto* src = reinterpret_cast<const uint8_t*>(upload.data.data());
        for (uint32_t y = 0; y < upload.height; ++y) {
            memcpy(dstPtr + y * rowSize, src + y * sourceRowPitch, rowSize);
        }
    }
    if (vmaFlushAllocation(allocator_, slice.allocation, slice.offset, dataSize) != VK_SUCCESS)
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan texture staging flush failed"));

    vk::Image image = dst->image();

    auto copyResult = executeCopy([&](vk::CommandBuffer cmd) {
        // eUndefined → eTransferDstOptimal
        vk::ImageMemoryBarrier b1;
        b1.srcAccessMask = {};
        b1.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        b1.oldLayout = vk::ImageLayout::eUndefined;
        b1.newLayout = vk::ImageLayout::eTransferDstOptimal;
        b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.image = image;
        b1.subresourceRange.aspectMask = VKTexture::isDepthFormat(upload.format) ? vk::ImageAspectFlagBits::eDepth
                                                                                 : vk::ImageAspectFlagBits::eColor;
        b1.subresourceRange.baseMipLevel = upload.mipLevel;
        b1.subresourceRange.levelCount = 1;
        b1.subresourceRange.baseArrayLayer = upload.arrayLayer;
        b1.subresourceRange.layerCount = 1;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {},
                            nullptr, b1);

        // buffer → image
        vk::BufferImageCopy region;
        region.bufferOffset = slice.offset;
        region.bufferRowLength = 0;  // staging 已紧密打包
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = b1.subresourceRange.aspectMask;
        region.imageSubresource.mipLevel = upload.mipLevel;
        region.imageSubresource.baseArrayLayer = upload.arrayLayer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(upload.width, upload.height, upload.depth);
        cmd.copyBufferToImage(slice.buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

        // eTransferDstOptimal → eShaderReadOnlyOptimal
        vk::ImageMemoryBarrier b2 = b1;
        b2.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        b2.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        b2.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        b2.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {},
                            nullptr, b2);
    });
    if (!copyResult)
        return std::unexpected(copyResult.error());

    dst->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    return {};
}

StagingSlice VKUploadContext::allocStaging(uint32_t size) {
    std::lock_guard lock(mutex_);

    // 同一个 staging buffer 中的 copy 区段保持 256 字节对齐。该值覆盖当前
    // buffer copy 与未压缩 texture copy 的对齐要求，也让后续扩展格式时行为稳定。
    constexpr uint32_t kStagingAlignment = 256;

    for (auto& slab : slabs_) {
        const uint32_t offset = alignUp(slab.used, kStagingAlignment);
        if (static_cast<uint64_t>(offset) + size <= slab.capacity) {
            StagingSlice slice;
            slice.buffer = slab.buffer;
            slice.allocation = slab.allocation;
            slice.mapped = slab.mapped;
            slice.offset = offset;
            slice.size = size;
            slab.used = offset + size;
            return slice;
        }
    }

    const uint32_t slabSize = (std::max) (uint32_t(4 * 1024 * 1024), alignUp(size, kStagingAlignment));
    slabs_.push_back(createSlab(slabSize));
    auto& slab = slabs_.back();

    StagingSlice slice;
    slice.buffer = slab.buffer;
    slice.allocation = slab.allocation;
    slice.mapped = slab.mapped;
    slice.offset = 0;
    slice.size = size;
    slab.used = size;
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
        (void) device_.waitForFences(upload_fence_, true, UINT64_MAX);
        (void) device_.resetFences(upload_fence_);
        pending_ = false;
        resetSlabs();
    }
}

ResultVoid VKUploadContext::beginUploadBatch() {
    if (batch_active_)
        return {};

    try {
        vk::CommandBufferAllocateInfo allocCI;
        allocCI.commandPool = cmd_pool_;
        allocCI.level = vk::CommandBufferLevel::ePrimary;
        allocCI.commandBufferCount = 1;
        auto cmds = device_.allocateCommandBuffers(allocCI);
        batch_cmd_ = cmds[0];

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        batch_cmd_.begin(beginInfo);
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, error.what()));
    }
    batch_active_ = true;
    return {};
}

ResultVoid VKUploadContext::flushUploadBatch() {
    if (!batch_active_)
        return {};

    try {
        batch_cmd_.end();

        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &batch_cmd_;
        queue_.submit(submitInfo, upload_fence_);

        const vk::Result waitResult = device_.waitForFences(upload_fence_, true, UINT64_MAX);
        if (waitResult != vk::Result::eSuccess)
            return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, "Vulkan upload fence wait failed"));
        device_.resetFences(upload_fence_);

        device_.freeCommandBuffers(cmd_pool_, batch_cmd_);
        device_.resetCommandPool(cmd_pool_);
    } catch (const vk::Error& error) {
        return std::unexpected(makeError(EngineErrorCode::ResourceUploadFailed, error.what()));
    }
    resetSlabs();

    batch_active_ = false;
    return {};
}

VKUploadContext::Slab VKUploadContext::createSlab(uint32_t size) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator_, &ci, &allocCI, &buffer, &allocation, &info) != VK_SUCCESS)
        return {};

    Slab slab;
    slab.buffer = vk::Buffer(buffer);
    slab.allocation = allocation;
    slab.mapped = info.pMappedData;
    slab.capacity = size;
    slab.used = 0;
    return slab;
}

uint32_t VKUploadContext::alignUp(uint32_t v, uint32_t align) {
    return (v + align - 1) & ~(align - 1);
}

}  // namespace mulan::engine
