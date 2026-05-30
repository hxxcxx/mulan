#include "VKDescriptorAllocator.h"

namespace mulan::engine {

VKDescriptorAllocator::VKDescriptorAllocator(vk::Device device,
                                             const PoolSizes& poolSizes)
    : m_device(device)
    , m_poolSizes(poolSizes)
{}

VKDescriptorAllocator::~VKDescriptorAllocator() {
    for (auto pool : m_pools) {
        m_device.destroyDescriptorPool(pool);
    }
}

void VKDescriptorAllocator::resetPools() {
    for (auto pool : m_pools) {
        m_device.resetDescriptorPool(pool);
    }
    m_activePool = nullptr;
}

VKDescriptorSet VKDescriptorAllocator::allocate(vk::DescriptorSetLayout layout) {
    if (!m_activePool) {
        m_activePool = getOrCreatePool();
    }

    vk::DescriptorSetAllocateInfo allocCI;
    allocCI.descriptorPool     = m_activePool;
    allocCI.descriptorSetCount = 1;
    allocCI.pSetLayouts       = &layout;

    try {
        auto sets = m_device.allocateDescriptorSets(allocCI);
        return VKDescriptorSet(m_device, sets[0]);
    } catch (vk::OutOfPoolMemoryError&) {
    } catch (vk::FragmentedPoolError&) {
    }

    m_activePool = createPool();
    allocCI.descriptorPool = m_activePool;
    auto sets = m_device.allocateDescriptorSets(allocCI);
    return VKDescriptorSet(m_device, sets[0]);
}

void VKDescriptorAllocator::bindUniformBuffer(vk::DescriptorSet set, uint32_t binding,
                                              vk::Buffer buffer, vk::DeviceSize offset,
                                              vk::DeviceSize range) {
    vk::DescriptorBufferInfo bufInfo;
    bufInfo.buffer = buffer;
    bufInfo.offset = offset;
    bufInfo.range  = (range == 0) ? VK_WHOLE_SIZE : range;

    vk::WriteDescriptorSet write;
    write.dstSet           = set;
    write.dstBinding       = binding;
    write.dstArrayElement  = 0;
    write.descriptorCount  = 1;
    write.descriptorType   = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo      = &bufInfo;

    m_device.updateDescriptorSets(1, &write, 0, nullptr);
}

void VKDescriptorAllocator::updateDescriptorSets(uint32_t writeCount,
                                                  const vk::WriteDescriptorSet* writes) {
    m_device.updateDescriptorSets(writeCount, writes, 0, nullptr);
}

vk::DescriptorPool VKDescriptorAllocator::getOrCreatePool() {
    if (!m_pools.empty()) {
        return m_pools.back();
    }
    return createPool();
}

vk::DescriptorPool VKDescriptorAllocator::createPool() {
    vk::DescriptorPoolCreateInfo poolCI;
    poolCI.flags         = {};
    poolCI.maxSets       = 1000;
    poolCI.poolSizeCount = static_cast<uint32_t>(m_poolSizes.sizes.size());
    poolCI.pPoolSizes    = m_poolSizes.sizes.data();

    auto pool = m_device.createDescriptorPool(poolCI);
    m_pools.push_back(pool);
    return pool;
}

VKDescriptorAllocator::PoolSizes VKDescriptorAllocator::defaultPoolSizes() {
    return {{
        { vk::DescriptorType::eUniformBuffer,         64 },
        { vk::DescriptorType::eCombinedImageSampler,  32 },
        { vk::DescriptorType::eStorageBuffer,         16 },
        { vk::DescriptorType::eUniformBufferDynamic,   8 },
    }};
}

} // namespace mulan::engine
