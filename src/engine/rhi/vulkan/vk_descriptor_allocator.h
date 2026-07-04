/**
 * @file vk_descriptor_allocator.h
 * @brief Vulkan Descriptor分配器，管理描述符池与集合分配
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "vk_common.h"
#include "vk_descriptor_set.h"

#include <vector>
#include <array>

namespace mulan::engine {

class VKDescriptorAllocator {
public:
    struct PoolSizes {
        std::vector<vk::DescriptorPoolSize> sizes;
    };

    VKDescriptorAllocator(vk::Device device,
                          const PoolSizes& poolSizes = defaultPoolSizes());
    ~VKDescriptorAllocator();

    void resetPools();
    VKDescriptorSet allocate(vk::DescriptorSetLayout layout);

    void bindUniformBuffer(vk::DescriptorSet set, uint32_t binding,
                           vk::Buffer buffer, vk::DeviceSize offset,
                           vk::DeviceSize range);
    void updateDescriptorSets(uint32_t writeCount,
                              const vk::WriteDescriptorSet* writes);

    static PoolSizes defaultPoolSizes();

    vk::Device device() const { return device_; }

private:
    vk::DescriptorPool getOrCreatePool();
    vk::DescriptorPool createPool();

    vk::Device                        device_;
    PoolSizes                         pool_sizes_;
    std::vector<vk::DescriptorPool>   pools_;
    vk::DescriptorPool                active_pool_ = nullptr;
};

} // namespace mulan::engine
