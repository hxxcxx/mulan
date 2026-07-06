/**
 * @file asset_gpu_registry.h
 * @brief AssetGpuRegistry —— 资产派生的不可变 GPU 资源注册表。
 * @author hxxcxx
 * @date 2026-07-06
 *
 * 设计：
 *  - 只管"资产派生 + 创建后不可变 + 独立 GPU 资源对象"（几何 buffer、贴图）。
 *    材质 UBO 不在此（它是帧动态/偏移寻址/有上限，由 MaterialCache 管）。
 *  - key 对本层不透明：由 view 层（RenderWorldSync）按资产身份生成，
 *    engine 全程透传不解释。这样 engine 不依赖 asset 层。
 *  - 懒加载：acquire 命中即返，miss 才上传。
 *  - 生命周期绑文档：文档切换时由 Renderer::setScene 触发 clear()，
 *    释放全部 GPU 资源（不做资产粒度 erase，因为 AssetLibrary::remove 当前零调用）。
 *
 *  - 贴图和几何同属资产派生的不可变 GPU 资源，统一在此管理。
 */
#pragma once

#include "render_geometry.h"
#include "texture_loader.h"
#include <mulan/core/result/error.h>
#include <mulan/graphics/mesh.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mulan::engine {

class RHIDevice;
class Texture;

class AssetGpuRegistry {
public:
    explicit AssetGpuRegistry(RHIDevice& device);

    AssetGpuRegistry(const AssetGpuRegistry&) = delete;
    AssetGpuRegistry& operator=(const AssetGpuRegistry&) = delete;

    /// 几何：按 key 查询，命中即返；miss 才用 mesh 上传（mesh 仅 miss 时被读）。
    /// key 由调用方（view 层）按资产身份生成，本层不解释。
    /// mesh 指向资产持有的稳定存储（文档存活期有效），上传后本层不持有该指针。
    const GpuGeometry* acquireGeometry(uint64_t key, const graphics::Mesh& mesh);

    /// 贴图：按来源 + 加载意图去重，命中即返；miss 时解码并上传。
    Texture* acquireTextureFromFile(const std::string& path, const TextureLoadOptions& options = {});
    Texture* acquireTextureFromMemory(const std::string& key, const std::byte* data, size_t size,
                                      const TextureLoadOptions& options = {});

    /// 创建由 registry 持有的独立 GPU 贴图，主要用于资产派生的程序化资源。
    Texture* createTexture(uint32_t width, uint32_t height, TextureFormat format, TextureUsageFlags usage,
                           const std::string& name = {});

    /// 清空全部资产派生 GPU 资源（文档切换时调用）。
    void clear();

    size_t geometryCount() const { return geometries_.size(); }
    size_t textureCount() const { return textures_.size(); }

private:
    struct GpuTextureResource {
        std::unique_ptr<Texture> texture;
        std::string source;
        uint32_t width = 0;
        uint32_t height = 0;

        GpuTextureResource() = default;
        explicit GpuTextureResource(std::unique_ptr<Texture> texture, std::string source);

        Texture* get() { return texture.get(); }
        const Texture* get() const { return texture.get(); }
        explicit operator bool() const { return texture != nullptr; }
    };

    static core::Result<GpuGeometry> createGpuBuffer(RHIDevice& device, const graphics::Mesh& mesh);
    static std::string textureKey(std::string_view sourceKind, const std::string& source,
                                  const TextureLoadOptions& options);

    std::unique_ptr<Texture> createRHITexture(const LoadedTexture& loaded, TextureUsageFlags usage, bool generateMips);

    RHIDevice& device_;
    std::unordered_map<uint64_t, GpuGeometry> geometries_;
    std::unordered_map<std::string, GpuTextureResource> textures_;
};

}  // namespace mulan::engine
