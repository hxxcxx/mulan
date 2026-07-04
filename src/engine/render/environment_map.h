/**
 * @file environment_map.h
 * @brief HDR 环境贴图加载与 GPU 纹理管理
 * @author hxxcxx
 * @date 2026-07-04
 */
#pragma once

#include "../rhi/texture.h"
#include "../rhi/device.h"

#include <memory>
#include <string>

namespace mulan::engine {

class RHIDevice;

/// HDR 环境贴图（equirectangular 格式，用于 IBL）
class EnvironmentMap {
public:
    EnvironmentMap() = default;
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    /// 从 .hdr 文件加载，创建 GPU 纹理（RGBA32_Float）
    bool load(RHIDevice& device, const std::string& path);

    /// 空状态时使用默认 1×1 占位纹理
    bool isValid() const { return texture_ != nullptr; }

    Texture* texture() const { return texture_.get(); }
    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }

private:
    std::unique_ptr<Texture> texture_;
    uint32_t width_  = 1;
    uint32_t height_ = 1;
};

} // namespace mulan::engine
