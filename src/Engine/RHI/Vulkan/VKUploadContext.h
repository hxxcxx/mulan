/**
 * @file VKUploadContext.h
 * @brief Vulkan上传管理器，Staging Buffer池与GPU传输
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "VkCommon.h"

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
        allocCI.commandPool        = m_cmdPool;
        allocCI.level              = vk::CommandBufferLevel::ePrimary;
        allocCI.commandBufferCount = 1;
        auto cmds = m_device.allocateCommandBuffers(allocCI);

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmds[0].begin(beginInfo);

        copyCmd(cmds[0]);

        cmds[0].end();

        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmds[0];
        m_queue.submit(submitInfo, m_uploadFence);

        m_device.waitForFences(m_uploadFence, true, UINT64_MAX);
        m_device.resetFences(m_uploadFence);

        m_device.freeCommandBuffers(m_cmdPool, cmds);
        m_device.resetCommandPool(m_cmdPool);
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

    vk::Device       m_device;
    VmaAllocator     m_allocator;
    uint32_t         m_queueFamily;
    vk::Queue        m_queue;

    vk::CommandPool  m_cmdPool;
    vk::Fence        m_uploadFence;
    bool             m_pending = false;

    std::vector<Slab> m_slabs;
    std::mutex        m_mutex;
};

} // namespace mulan::Engine
