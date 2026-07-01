/**
 * @file vk_upload_context.h
 * @brief Vulkan上传管理器，Staging Buffer池与GPU传输
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "vk_common.h"

#include <vector>
#include <mutex>
#include <functional>

namespace mulan::engine {

class VKBuffer;

struct StagingSlice {
    vk::Buffer      buffer     = {};
    VmaAllocation   allocation = nullptr;
    void*           mapped     = nullptr;
    uint32_t        offset     = 0;
    uint32_t        size       = 0;
};

class VKUploadContext {
public:
    VKUploadContext(vk::Device device, VmaAllocator allocator,
                    uint32_t queueFamily, vk::Queue queue);
    ~VKUploadContext();

    void uploadToBuffer(VKBuffer* dst, const void* data, uint32_t size,
                        uint32_t dstOffset = 0);
    void uploadBufferInit(VKBuffer* dst);

    StagingSlice allocStaging(uint32_t size);
    void resetSlabs();
    void flush();

private:
    template <typename F>
    void executeCopy(F&& copyCmd) {
        vk::CommandBufferAllocateInfo allocCI;
        allocCI.commandPool        = cmd_pool_;
        allocCI.level              = vk::CommandBufferLevel::ePrimary;
        allocCI.commandBufferCount = 1;
        auto cmds = device_.allocateCommandBuffers(allocCI);

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmds[0].begin(beginInfo);

        copyCmd(cmds[0]);

        cmds[0].end();

        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmds[0];
        queue_.submit(submitInfo, upload_fence_);

        device_.waitForFences(upload_fence_, true, UINT64_MAX);
        device_.resetFences(upload_fence_);

        device_.freeCommandBuffers(cmd_pool_, cmds);
        device_.resetCommandPool(cmd_pool_);
        resetSlabs();
    }

    struct Slab {
        vk::Buffer      buffer;
        VmaAllocation   allocation = nullptr;
        void*           mapped     = nullptr;
        uint32_t        capacity   = 0;
        uint32_t        used       = 0;
    };

    Slab createSlab(uint32_t size);
    static uint32_t alignUp(uint32_t v, uint32_t align);

    vk::Device       device_;
    VmaAllocator     allocator_;
    uint32_t         queue_family_;
    vk::Queue        queue_;

    vk::CommandPool  cmd_pool_;
    vk::Fence        upload_fence_;
    bool             pending_ = false;

    std::vector<Slab> slabs_;
    std::mutex        mutex_;
};

} // namespace mulan::engine
