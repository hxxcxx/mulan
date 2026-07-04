#include "environment_map.h"

// stb_image HDR 加载（Core DLL 不导出 stb C 函数，Engine 静态库需独立实现）
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdio>

namespace mulan::engine {

EnvironmentMap::~EnvironmentMap() {
    texture_.reset();
}

bool EnvironmentMap::load(RHIDevice& device, const std::string& path) {
    int w = 0, h = 0, comp = 0;
    float* data = stbi_loadf(path.c_str(), &w, &h, &comp, 4); // force RGBA
    if (!data) {
        std::fprintf(stderr, "[EnvMap] Failed to load HDR: %s\n", path.c_str());
        return false;
    }

    width_  = static_cast<uint32_t>(w);
    height_ = static_cast<uint32_t>(h);

    TextureDesc desc;
    desc.name      = "EnvMap";
    desc.format    = TextureFormat::RGBA32_Float;
    desc.width     = width_;
    desc.height    = height_;
    desc.depth     = 1;
    desc.dimension = TextureDimension::Texture2D;
    desc.usage     = TextureUsageFlags::ShaderResource;

    auto result = device.createTexture(desc);
    if (!result) {
        std::fprintf(stderr, "[EnvMap] createTexture failed: %s\n",
                     result.error().message.c_str());
        return false;
    }
    texture_ = std::move(*result);

    device.uploadTextureData(texture_.get(), data, width_, height_, TextureFormat::RGBA32_Float);

    stbi_image_free(data);
    return true;
}

} // namespace mulan::engine
