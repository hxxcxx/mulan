#include "vk_descriptor_allocator.h"

namespace mulan::engine {

VKDescriptorAllocator::VKDescriptorAllocator(vk::Device device,
                                             const PoolSizes& poolSizes)
    : device_(device)
    , pool_sizes_(poolSizes)
{}

VKDescriptorAllocator::~VKDescriptorAllocator() {
    for (auto pool : pools_) {
        device_.destroyDescriptorPool(pool);
    }
}

void VKDescriptorAllocator::resetPools() {
    for (auto pool : pools_) {
        device_.resetDescriptorPool(pool);
    }
    active_pool_ = nullptr;
}

VKDescriptorSet VKDescriptorAllocator::allocate(vk::DescriptorSetLayout layout) {
    if (!active_pool_) {
        active_pool_ = getOrCreatePool();
    }

    vk::DescriptorSetAllocateInfo allocCI;
    allocCI.descriptorPool     = active_pool_;
    allocCI.descriptorSetCount = 1;
    allocCI.pSetLayouts       = &layout;

    try {
        auto sets = device_.allocateDescriptorSets(allocCI);
        return VKDescriptorSet(device_, sets[0]);
    } catch (vk::OutOfPoolMemoryError&) {
    } catch (vk::FragmentedPoolError&) {
    }

    active_pool_ = createPool();
    allocCI.descriptorPool = active_pool_;
    auto sets = device_.allocateDescriptorSets(allocCI);
    return VKDescriptorSet(device_, sets[0]);
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

    device_.updateDescriptorSets(1, &write, 0, nullptr);
}

void VKDescriptorAllocator::updateDescriptorSets(uint32_t writeCount,
                                                  const vk::WriteDescriptorSet* writes) {
    device_.updateDescriptorSets(writeCount, writes, 0, nullptr);
}

vk::DescriptorPool VKDescriptorAllocator::getOrCreatePool() {
    if (!pools_.empty()) {
        return pools_.back();
    }
    return createPool();
}

vk::DescriptorPool VKDescriptorAllocator::createPool() {
    vk::DescriptorPoolCreateInfo poolCI;
    poolCI.flags         = {};
    poolCI.maxSets       = 1000;
    poolCI.poolSizeCount = static_cast<uint32_t>(pool_sizes_.sizes.size());
    poolCI.pPoolSizes    = pool_sizes_.sizes.data();

    auto pool = device_.createDescriptorPool(poolCI);
    pools_.push_back(pool);
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
