/**
 * @file device_resource_service.cpp
 * @brief DeviceResourceService 的共享资源所有权与可靠准备实现
 * @author hxxcxx
 * @date 2026-07-15
 */

#include "device_resource_service.h"

#include "draw/geometry_draw_shared_resources.h"
#include "material/material_cache.h"
#include "../rhi/device.h"

#include <mulan/core/log/log.h>

#include <optional>

namespace mulan::engine {

DeviceResourceService::DeviceResourceService(RHIDevice& device)
    : device_(device), asset_registry_(device), pipeline_library_(device) {
}

DeviceResourceService::~DeviceResourceService() {
    // 共享管线和默认资源可能仍被最后一帧引用；先确认 Device 空闲并回收延迟队列，
    // 再由成员逆序析构，保证所有 GPU 对象都早于 Device 销毁。
    if (auto idle = device_.waitIdle(); !idle) {
        LOG_ERROR("[DeviceResourceService] Waiting for device idle during shutdown failed: {}", idle.error().message);
    }
    device_.collectGarbage();
}

bool DeviceResourceService::init() {
    if (initialized_) {
        return true;
    }
    material_cache_ = std::make_unique<MaterialCache>();
    geometry_resources_ = std::make_unique<GeometryDrawSharedResources>(device_, *material_cache_);
    initialized_ = geometry_resources_->init();
    return initialized_;
}

DeviceResourceClientId DeviceResourceService::registerClient() {
    DeviceResourceClientId client = next_client_++;
    if (client == 0) {
        client = next_client_++;
    }
    clients_.insert(client);
    return client;
}

bool DeviceResourceService::validClient(DeviceResourceClientId client) const {
    return client != 0 && clients_.contains(client);
}

ResultVoid DeviceResourceService::preparePersistentResources(DeviceResourceClientId client,
                                                             const RenderResourcePrepareList& prepare) {
    if (!initialized_ || !validClient(client)) {
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "Device resource client is not active."));
    }
    if (prepare.empty()) {
        return {};
    }
    if (auto result = device_.beginUploadBatch(); !result) {
        return std::unexpected(result.error());
    }

    std::optional<Error> failure;
    for (const RenderGeometryPrepareDesc& geometry : prepare.geometries()) {
        if (geometry.isRetire()) {
            const auto known = geometry_owners_.find(geometry.resourceKey);
            if (known != geometry_owners_.end() && known->second.erase(client) > 0 && known->second.empty()) {
                geometry_owners_.erase(known);
                if (auto retired = asset_registry_.retireGeometry(geometry.resourceKey); !retired) {
                    failure = retired.error();
                    break;
                }
            }
            continue;
        }
        if (!geometry.mesh || geometry.mesh->empty()) {
            continue;
        }
        auto acquired = asset_registry_.acquireGeometry(geometry.resourceKey, *geometry.mesh, geometry.forceUpdate);
        if (!acquired) {
            failure = acquired.error();
            break;
        }
        geometry_owners_[geometry.resourceKey].insert(client);
    }
    if (!failure) {
        for (const RenderTexturePrepareDesc& texture : prepare.textures()) {
            TextureLoadOptions options;
            options.sRGB = texture.identity.srgb;
            options.generateMips = texture.identity.generateMips;
            if (texture.isRetire()) {
                const auto known = texture_owners_.find(texture.identity);
                if (known != texture_owners_.end() && known->second.erase(client) > 0 && known->second.empty()) {
                    texture_owners_.erase(known);
                    if (auto retired = asset_registry_.retireTexture(texture.identity.resourceKey, options); !retired) {
                        failure = retired.error();
                        break;
                    }
                }
                continue;
            }
            if (!texture.image || !texture.image->valid()) {
                continue;
            }
            auto acquired = asset_registry_.acquireTexture(texture.identity.resourceKey, *texture.image, options,
                                                           texture.contentRevision);
            if (!acquired) {
                failure = acquired.error();
                break;
            }
            texture_owners_[texture.identity].insert(client);
        }
    }

    if (auto flushed = device_.flushUploadBatch(); !flushed) {
        return std::unexpected(flushed.error());
    }
    asset_registry_.releaseUploadFailureKeepalives();
    rebuildDomainReferences();
    if (failure) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

ResultVoid DeviceResourceService::releaseClient(DeviceResourceClientId client) {
    if (!validClient(client)) {
        return {};
    }
    std::optional<Error> failure;
    for (auto it = geometry_owners_.begin(); it != geometry_owners_.end();) {
        it->second.erase(client);
        if (!it->second.empty()) {
            ++it;
            continue;
        }
        const RenderResourceKey key = it->first;
        it = geometry_owners_.erase(it);
        if (auto retired = asset_registry_.retireGeometry(key); !retired && !failure) {
            failure = retired.error();
        }
    }
    for (auto it = texture_owners_.begin(); it != texture_owners_.end();) {
        it->second.erase(client);
        if (!it->second.empty()) {
            ++it;
            continue;
        }
        const RenderTextureResourceKey identity = it->first;
        it = texture_owners_.erase(it);
        TextureLoadOptions options;
        options.sRGB = identity.srgb;
        options.generateMips = identity.generateMips;
        if (auto retired = asset_registry_.retireTexture(identity.resourceKey, options); !retired && !failure) {
            failure = retired.error();
        }
    }
    clients_.erase(client);
    if (clients_.empty() && material_cache_) {
        material_cache_->clear();
    }
    rebuildDomainReferences();
    if (failure) {
        LOG_ERROR("[DeviceResourceService] Client resource retirement failed: {}", failure->message);
        return std::unexpected(std::move(*failure));
    }
    return {};
}

void DeviceResourceService::rebuildDomainReferences() {
    domain_references_.clear();
    for (const auto& [key, owners] : geometry_owners_) {
        domain_references_[key.domain] += owners.size();
    }
    for (const auto& [identity, owners] : texture_owners_) {
        domain_references_[identity.resourceKey.domain] += owners.size();
    }
}

DeviceResourceStats DeviceResourceService::stats() const {
    return DeviceResourceStats{
        .clientCount = clients_.size(),
        .domainCount = domain_references_.size(),
        .geometryCount = asset_registry_.geometryCount(),
        .textureCount = asset_registry_.textureCount(),
        .pipelineCount = pipeline_library_.size(),
    };
}

}  // namespace mulan::engine
