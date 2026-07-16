/**
 * @file device_pipeline_library.h
 * @brief DevicePipelineLibrary 按完整目标签名共享不可变图形管线
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include "technique/render_technique.h"
#include "../rhi/texture.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace mulan::engine {

class PipelineState;
class RHIDevice;
class Shader;

enum class ObjectBindingMode : uint8_t {
    Single,
    InstancedBatch,
};

struct DevicePipelineKey {
    RenderTechnique technique = RenderTechnique::SolidLit;
    TextureFormat colorFormat = TextureFormat::Unknown;
    TextureFormat depthFormat = TextureFormat::Unknown;
    uint32_t sampleCount = 1;
    bool hasDepth = true;
    ObjectBindingMode objectBindingMode = ObjectBindingMode::Single;

    bool operator==(const DevicePipelineKey&) const = default;
};

struct DevicePipelineKeyHash {
    size_t operator()(const DevicePipelineKey& key) const noexcept;
};

class DevicePipelineLibrary {
public:
    explicit DevicePipelineLibrary(RHIDevice& device) : device_(device) {}
    ~DevicePipelineLibrary();

    PipelineState* acquire(const DevicePipelineKey& key);
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::unique_ptr<Shader> vertexShader;
        std::unique_ptr<Shader> pixelShader;
        std::unique_ptr<PipelineState> pipeline;
    };

    RHIDevice& device_;
    std::unordered_map<DevicePipelineKey, Entry, DevicePipelineKeyHash> entries_;
};

}  // namespace mulan::engine
