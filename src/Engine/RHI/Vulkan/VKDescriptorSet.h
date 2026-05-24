/**
 * @file VKDescriptorSet.h
 * @brief Vulkan DescriptorSet 轻量封装，收集写入操作并批量提交
 * @author hxxcxx
 * @date 2026-05-13
 */

#pragma once

#include "VkCommon.h"

#include <array>
#include <cstdint>

namespace mulan::engine {

/// 轻量描述符集封装：收集写入操作，一次性 flush 到 GPU
class VKDescriptorSet {
public:
    static constexpr uint8_t kMaxWrites = 16;

    VKDescriptorSet(vk::Device device, vk::DescriptorSet set)
        : m_device(device), m_set(set) {}

    vk::DescriptorSet vkSet() const { return m_set; }

    void writeUBO(uint32_t binding, vk::Buffer buffer,
                  vk::DeviceSize offset, vk::DeviceSize range) {
        auto idx = m_writeCount;
        m_bufInfos[idx] = vk::DescriptorBufferInfo(buffer, offset, range);
        m_writes[idx] = vk::WriteDescriptorSet(
            m_set, binding, 0, 1,
            vk::DescriptorType::eUniformBuffer,
            nullptr, &m_bufInfos[idx], nullptr);
        ++m_writeCount;
    }

    void writeSampledImage(uint32_t binding, vk::ImageView imageView,
                           vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal) {
        auto idx = m_writeCount;
        m_imgInfos[idx] = vk::DescriptorImageInfo(nullptr, imageView, layout);
        m_writes[idx] = vk::WriteDescriptorSet(
            m_set, binding, 0, 1,
            vk::DescriptorType::eSampledImage,
            &m_imgInfos[idx], nullptr, nullptr);
        ++m_writeCount;
    }

    /// 批量提交所有收集的写入操作
    void flush() {
        if (m_writeCount > 0) {
            m_device.updateDescriptorSets(m_writeCount, m_writes.data(), 0, nullptr);
        }
    }

    /// 绑定到图形管线
    void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
              uint32_t setIndex = 0) const {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               layout, setIndex, 1, &m_set, 0, nullptr);
    }

private:
    vk::Device                                       m_device;
    vk::DescriptorSet                                m_set;
    uint8_t                                          m_writeCount = 0;
    std::array<vk::DescriptorBufferInfo, kMaxWrites> m_bufInfos;
    std::array<vk::DescriptorImageInfo,  kMaxWrites> m_imgInfos;
    std::array<vk::WriteDescriptorSet,   kMaxWrites> m_writes;
};

} // namespace mulan::Engine
