/**
 * @file device_resource_service.h
 * @brief DeviceResourceService 管理同一 RHI Device 上可跨视图共享的 GPU 资源
 * @author hxxcxx
 * @date 2026-07-15
 */

#pragma once

#include "asset_gpu_registry.h"
#include "device_pipeline_library.h"
#include "frontend/render_resource_prepare.h"

#include <mulan/core/result/error.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace mulan::engine {

class GeometryDrawSharedResources;
class MaterialCache;
class RHIDevice;
class TextStage;
struct RenderTargetInfo;

using DeviceResourceClientId = uint64_t;

struct DeviceResourceStats {
    size_t clientCount = 0;
    size_t domainCount = 0;
    size_t geometryCount = 0;
    size_t textureCount = 0;
    size_t pipelineCount = 0;
    size_t textStageCount = 0;
};

class DeviceResourceService {
public:
    explicit DeviceResourceService(RHIDevice& device);
    ~DeviceResourceService();

    DeviceResourceService(const DeviceResourceService&) = delete;
    DeviceResourceService& operator=(const DeviceResourceService&) = delete;

    bool init();
    DeviceResourceClientId registerClient();
    ResultVoid releaseClient(DeviceResourceClientId client);
    ResultVoid preparePersistentResources(DeviceResourceClientId client, const RenderResourcePrepareList& prepare);

    AssetGpuRegistry& assets() { return asset_registry_; }
    MaterialCache& materials() { return *material_cache_; }
    GeometryDrawSharedResources& geometryDrawResources() { return *geometry_resources_; }
    DevicePipelineLibrary& pipelines() { return pipeline_library_; }
    /// TextStage 的字体图集、文本管线和动态缓冲均属于 Device 级串行执行资源。
    /// 相同目标签名的 Renderer 只借用同一实例，不再按视图重复创建。
    TextStage* acquireTextStage(const RenderTargetInfo& target);
    DeviceResourceStats stats() const;

private:
    using ClientSet = std::unordered_set<DeviceResourceClientId>;

    struct TextStageKey {
        TextureFormat colorFormat = TextureFormat::Unknown;
        TextureFormat depthFormat = TextureFormat::Unknown;
        uint32_t sampleCount = 1;
        bool hasDepth = true;

        bool operator==(const TextStageKey&) const = default;
    };

    struct TextStageKeyHash {
        size_t operator()(const TextStageKey& key) const noexcept;
    };

    bool validClient(DeviceResourceClientId client) const;
    void rebuildDomainReferences();

    RHIDevice& device_;
    AssetGpuRegistry asset_registry_;
    DevicePipelineLibrary pipeline_library_;
    std::unique_ptr<MaterialCache> material_cache_;
    std::unique_ptr<GeometryDrawSharedResources> geometry_resources_;
    std::unordered_map<TextStageKey, std::unique_ptr<TextStage>, TextStageKeyHash> text_stages_;
    std::unordered_set<DeviceResourceClientId> clients_;
    std::unordered_map<RenderResourceKey, ClientSet> geometry_owners_;
    std::unordered_map<RenderTextureResourceKey, ClientSet, RenderTextureResourceKeyHash> texture_owners_;
    std::unordered_map<ResourceDomainId, size_t> domain_references_;
    DeviceResourceClientId next_client_ = 1;
    bool initialized_ = false;
};

}  // namespace mulan::engine
