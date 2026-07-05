/**
 * @file vk_descriptor_set.h
 * @brief Vulkan DescriptorSet 轻量封装，收集写入操作并批量提交
 * @author hxxcxx
 * @date 2026-05-13
 */

#pragma once

#include "vk_common.h"

#include <array>
#include <cstdint>

namespace mulan::engine {

/// 轻量描述符集封装：收集写入操作，一次性 flush 到 GPU
class VKDescriptorSet {
public:
    static constexpr uint8_t kMaxWrites = 16;

    VKDescriptorSet(vk::Device device, vk::DescriptorSet set) : device_(device), set_(set) {}

    vk::DescriptorSet vkSet() const { return set_; }

    void writeUBO(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range) {
        auto idx = write_count_;
        buf_infos_[idx] = vk::DescriptorBufferInfo(buffer, offset, range);
        writes_[idx] = vk::WriteDescriptorSet(set_, binding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr,
                                              &buf_infos_[idx], nullptr);
        ++write_count_;
    }

    void writeSampledImage(uint32_t binding, vk::ImageView imageView,
                           vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal) {
        auto idx = write_count_;
        img_infos_[idx] = vk::DescriptorImageInfo(nullptr, imageView, layout);
        writes_[idx] = vk::WriteDescriptorSet(set_, binding, 0, 1, vk::DescriptorType::eSampledImage, &img_infos_[idx],
                                              nullptr, nullptr);
        ++write_count_;
    }

    void writeSampler(uint32_t binding, vk::Sampler sampler) {
        auto idx = write_count_;
        img_infos_[idx] = vk::DescriptorImageInfo(sampler, nullptr, vk::ImageLayout::eUndefined);
        writes_[idx] = vk::WriteDescriptorSet(set_, binding, 0, 1, vk::DescriptorType::eSampler, &img_infos_[idx],
                                              nullptr, nullptr);
        ++write_count_;
    }

    /// 批量提交所有收集的写入操作
    void flush() {
        if (write_count_ > 0) {
            device_.updateDescriptorSets(write_count_, writes_.data(), 0, nullptr);
        }
    }

    /// 绑定到图形管线
    void bind(vk::CommandBuffer cmd, vk::PipelineLayout layout, uint32_t setIndex = 0) const {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, setIndex, 1, &set_, 0, nullptr);
    }

private:
    vk::Device device_;
    vk::DescriptorSet set_;
    uint8_t write_count_ = 0;
    std::array<vk::DescriptorBufferInfo, kMaxWrites> buf_infos_;
    std::array<vk::DescriptorImageInfo, kMaxWrites> img_infos_;
    std::array<vk::WriteDescriptorSet, kMaxWrites> writes_;
};

}  // namespace mulan::engine
