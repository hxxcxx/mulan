/**
 * @file vk_upload_context.h
 * @brief Vulkan上传管理器，Staging Buffer池与GPU传输
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "vk_common.h"

#include "../rhi/texture.h"

#include <vector>
#include <mutex>
#include <functional>

namespace mulan::engine {

class VKBuffer;
class VKTexture;

struct StagingSlice {
    vk::Buffer buffer = {};
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
    uint32_t offset = 0;
    uint32_t size = 0;
};

class VKUploadContext {
public:
    VKUploadContext(vk::Device device, VmaAllocator allocator, uint32_t queueFamily, vk::Queue queue);
    ~VKUploadContext();

    void uploadToBuffer(VKBuffer* dst, const void* data, uint32_t size, uint32_t dstOffset = 0);
    void uploadBufferInit(VKBuffer* dst);

    /// 上传像素数据到纹理：staging 拷贝 + layout 转换到 eShaderReadOnlyOptimal。
    /// 同步等待 GPU 完成。仅支持单 mip、非压缩颜色格式。
    void uploadTexture(VKTexture* dst, const void* data, uint32_t width, uint32_t height, TextureFormat format);

    StagingSlice allocStaging(uint32_t size);
    void resetSlabs();
    void flush();

    /// 开始批量上传：后续 uploadToBuffer/uploadTexture 只录制不提交
    void beginUploadBatch();

    /// 结束批量上传：提交一次命令并同步等待 GPU 完成
    void flushUploadBatch();

private:
    template <typename F>
    void executeCopy(F&& copyCmd) {
        if (batch_active_) {
            // 批量模式：只录制到 batch_cmd_，不提交
            copyCmd(batch_cmd_);
            return;
        }

        vk::CommandBufferAllocateInfo allocCI;
        allocCI.commandPool = cmd_pool_;
        allocCI.level = vk::CommandBufferLevel::ePrimary;
        allocCI.commandBufferCount = 1;
        auto cmds = device_.allocateCommandBuffers(allocCI);

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmds[0].begin(beginInfo);

        copyCmd(cmds[0]);

        cmds[0].end();

        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmds[0];
        queue_.submit(submitInfo, upload_fence_);

        device_.waitForFences(upload_fence_, true, UINT64_MAX);
        device_.resetFences(upload_fence_);

        device_.freeCommandBuffers(cmd_pool_, cmds);
        device_.resetCommandPool(cmd_pool_);
        resetSlabs();
    }

    struct Slab {
        vk::Buffer buffer;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr;
        uint32_t capacity = 0;
        uint32_t used = 0;
    };

    Slab createSlab(uint32_t size);
    static uint32_t alignUp(uint32_t v, uint32_t align);

    vk::Device device_;
    VmaAllocator allocator_;
    uint32_t queue_family_;
    vk::Queue queue_;

    vk::CommandPool cmd_pool_;
    vk::Fence upload_fence_;
    bool pending_ = false;

    // 批量上传状态
    bool batch_active_ = false;
    vk::CommandBuffer batch_cmd_;

    std::vector<Slab> slabs_;
    std::mutex mutex_;
};

}  // namespace mulan::engine
