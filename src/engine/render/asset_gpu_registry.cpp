#include "asset_gpu_registry.h"
#include "../rhi/buffer.h"
#include "../rhi/device.h"

namespace mulan::engine {

AssetGpuRegistry::AssetGpuRegistry(RHIDevice& device) : device_(device) {
}

const GpuGeometry* AssetGpuRegistry::acquireGeometry(uint64_t key, const graphics::Mesh& mesh) {
    if (auto it = geometries_.find(key); it != geometries_.end() && it->second.isValid()) {
        return &it->second;  // 命中：不碰 mesh
    }
    auto result = createGpuBuffer(device_, mesh);
    if (!result) {
        return nullptr;
    }
    auto [inserted, _] = geometries_.emplace(key, std::move(*result));
    return &inserted->second;
}

void AssetGpuRegistry::clear() {
    geometries_.clear();
}

core::Result<GpuGeometry> AssetGpuRegistry::createGpuBuffer(RHIDevice& device, const graphics::Mesh& mesh) {
    GpuGeometry geo;
    if (mesh.empty()) {
        return geo;
    }

    geo.layout = mesh.layout;
    geo.vertexStride = mesh.vertexStride();
    geo.vertexCount = mesh.vertexCount();
    geo.indexCount = mesh.indexCount();
    geo.indexType = mesh.indexType;

    if (geo.vertexCount > 0 && !mesh.vertices.empty()) {
        const uint32_t size = static_cast<uint32_t>(mesh.vertices.size());
        auto vb = device.createBuffer(BufferDesc::vertex(size, mesh.vertices.data(), "AssetGpuRegistryVB"));
        if (!vb) {
            return std::unexpected(vb.error());
        }
        geo.vertexBuffer = std::move(*vb);
    }

    if (geo.indexCount > 0 && !mesh.indices.empty()) {
        const uint32_t size = static_cast<uint32_t>(mesh.indices.size());
        auto ib = device.createBuffer(BufferDesc::index(size, mesh.indices.data(), "AssetGpuRegistryIB"));
        if (!ib) {
            return std::unexpected(ib.error());
        }
        geo.indexBuffer = std::move(*ib);
    }

    geo.uploaded = true;
    return geo;
}

}  // namespace mulan::engine
