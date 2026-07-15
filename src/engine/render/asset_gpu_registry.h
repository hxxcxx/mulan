/**
 * @file asset_gpu_registry.h
 * @brief AssetGpuRegistry —— 资产派生的不可变 GPU 资源注册表。
 * @author hxxcxx
 * @date 2026-07-06
 *
 * 设计：
 *  - 只管"资产派生 + 创建后不可变 + 独立 GPU 资源对象"（几何缓冲、贴图）。
 *    材质参数由渲染命令按需写入瞬态 Uniform，不属于资产资源。
 *  - key 使用 AssetGpuKey 强类型封装：由 view 层（RenderWorldSync）按资产身份或
 *    临时预览角色槽位生成，engine 只校验有效性并透传，不依赖 asset 层。
 *  - 懒加载：acquire 命中即返，miss 才上传。
 *  - 生命周期绑资产域：资产域切换时整体清理；普通场景差量按 key 退役。
 *    退役资源以最近一次 submission token 延迟销毁，不与 GPU 在途访问竞争。
 *
 *  - 贴图和几何同属资产派生的不可变 GPU 资源，统一在此管理。
 */
#pragma once

#include "asset_gpu_key.h"
#include "render_geometry.h"
#include "../rhi/texture.h"
#include <mulan/core/image/image.h>
#include <mulan/core/result/error.h>
#include <mulan/graphics/mesh.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace mulan::engine {

class RHIDevice;
class Texture;

struct TextureLoadOptions {
    bool generateMips = true;
    bool sRGB = false;
};

class AssetGpuRegistry {
public:
    explicit AssetGpuRegistry(RHIDevice& device);

    AssetGpuRegistry(const AssetGpuRegistry&) = delete;
    AssetGpuRegistry& operator=(const AssetGpuRegistry&) = delete;

    /// 几何：按 key 查询，命中即返；miss 才用 mesh 上传（mesh 仅 miss 时被读）。
    /// key 由调用方（view 层）按资产身份或稳定临时槽位生成，本层只校验有效性，不解释其语义。
    /// mesh 指向资产持有的稳定存储（文档存活期有效），上传后本层不持有该指针。
    core::Result<const GpuGeometry*> acquireGeometry(AssetGpuKey key, const graphics::Mesh& mesh,
                                                     bool forceUpdate = false);

    /// 按 key 移除几何并延迟到最近一次 GPU 提交完成后销毁。
    /// 返回 false 表示 key 本就不存在，该情况是幂等成功。
    core::Result<bool> retireGeometry(AssetGpuKey key);

    /// 贴图：按资产身份 + 加载意图 + 内容版本去重。
    /// 同 key/options 且版本相同时复用；版本变化时上传新实例并延迟退役旧实例。
    core::Result<Texture*> acquireTexture(AssetGpuKey key, const core::Image& image,
                                          const TextureLoadOptions& options = {}, uint64_t contentRevision = 0);

    /// 按资产键与加载意图移除单个贴图实例，并延迟到最近一次 GPU 提交完成后销毁。
    /// 返回 false 表示完整身份本就不存在，该情况是幂等成功。
    core::Result<bool> retireTexture(AssetGpuKey key, const TextureLoadOptions& options = {});

    /// 上传批次同步结束后释放失败路径保活对象；调用前必须保证批次已经 flush 成功。
    void releaseUploadFailureKeepalives();

    /// 查询已准备好的几何资源；不会触发 GPU 创建或上传。
    const GpuGeometry* findGeometry(AssetGpuKey key) const;

    /// 查询已准备好的贴图资源；不会触发 GPU 创建或上传。
    Texture* findTexture(AssetGpuKey key, const TextureLoadOptions& options = {});

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
        uint64_t contentRevision = 0;

        GpuTextureResource() = default;
        explicit GpuTextureResource(std::unique_ptr<Texture> texture, std::string source, uint64_t contentRevision = 0);

        Texture* get() { return texture.get(); }
        const Texture* get() const { return texture.get(); }
        explicit operator bool() const { return texture != nullptr; }
    };

    core::Result<GpuGeometry> createGpuBuffer(const graphics::Mesh& mesh);
    core::Result<void> retireGeometryResource(GpuGeometry geometry);
    core::Result<void> retireTextureResource(std::unique_ptr<Texture> texture);
    static std::string textureKey(AssetGpuKey resourceKey, const TextureLoadOptions& options);
    static TextureFormat toRHITextureFormat(core::PixelFormat pixelFmt, bool sRGB);

    core::Result<std::unique_ptr<Texture>> createRHITexture(const core::Image& image, TextureUsageFlags usage,
                                                            bool sRGB, bool generateMips);

    RHIDevice& device_;
    std::unordered_map<AssetGpuKey, GpuGeometry> geometries_;
    std::unordered_map<std::string, GpuTextureResource> textures_;
    std::optional<GpuGeometry> retirement_failure_keepalive_;
    std::shared_ptr<Texture> retirement_failure_texture_keepalive_;

    // 批次上传可能已经录制了指向这些对象的命令。即使后续资源失败，
    // 也必须保活到 flush 同步结束，不能让局部 unique_ptr 提前析构。
    // 单个批次遇到首个错误便停止，因此固定槽即可覆盖失败路径且不会再次分配内存。
    std::optional<GpuGeometry> failed_upload_geometry_;
    std::unique_ptr<Texture> failed_upload_texture_;
};

}  // namespace mulan::engine
