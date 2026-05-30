/**
 * @file VKDescriptorAllocator.h
 * @brief Vulkan Descriptor分配器，管理描述符池与集合分配
 * @author hxxcxx
 * @date 2026-04-16
 */

#pragma once

#include "VkCommon.h"
#include "VKDescriptorSet.h"

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

private:
    vk::DescriptorPool getOrCreatePool();
    vk::DescriptorPool createPool();

    vk::Device                        m_device;
    PoolSizes                         m_poolSizes;
    std::vector<vk::DescriptorPool>   m_pools;
    vk::DescriptorPool                m_activePool = nullptr;
};

} // namespace mulan::engine
