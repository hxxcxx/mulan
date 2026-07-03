#include "render_resource_cache.h"
#include "../rhi/device.h"
#include "../rhi/buffer.h"

#include <mulan/core/log/log.h>

namespace mulan::engine {

RenderResourceCache::RenderResourceCache(RHIDevice& device)
    : device_(device) {
}

void RenderResourceCache::uploadFaceGeometry(uint64_t key, const Mesh& mesh) {
    auto result = createGpuBuffer(device_, mesh);
    if (!result) {
        return;
    }
    face_geos_[key] = std::move(*result);
}

void RenderResourceCache::uploadEdgeGeometry(uint64_t key, const Mesh& mesh) {
    auto result = createGpuBuffer(device_, mesh);
    if (!result) {
        return;
    }
    edge_geos_[key] = std::move(*result);
}

void RenderResourceCache::releaseResource(uint64_t key) {
    face_geos_.erase(key);
    edge_geos_.erase(key);
}

bool RenderResourceCache::hasResource(uint64_t key) const {
    auto it = face_geos_.find(key);
    if (it != face_geos_.end() && it->second.isValid()) return true;
    auto eit = edge_geos_.find(key);
    return eit != edge_geos_.end() && eit->second.isValid();
}

const GpuGeometry* RenderResourceCache::faceGeometry(uint64_t key) const {
    auto it = face_geos_.find(key);
    return it != face_geos_.end() && it->second.isValid() ? &it->second : nullptr;
}

const GpuGeometry* RenderResourceCache::edgeGeometry(uint64_t key) const {
    auto it = edge_geos_.find(key);
    return it != edge_geos_.end() && it->second.isValid() ? &it->second : nullptr;
}

void RenderResourceCache::clear() {
    face_geos_.clear();
    edge_geos_.clear();
}

std::expected<GpuGeometry, core::Error>
RenderResourceCache::createGpuBuffer(RHIDevice& device, const Mesh& mesh) {
    GpuGeometry geo;
    if (mesh.empty()) return geo;

    geo.layout       = mesh.layout;
    geo.vertexStride = mesh.vertexStride();
    geo.vertexCount  = mesh.vertexCount();
    geo.indexCount   = mesh.indexCount();

    if (geo.vertexCount > 0 && !mesh.vertices.empty()) {
        uint32_t size = static_cast<uint32_t>(mesh.vertices.size());
        auto vb = device.createBuffer(
            BufferDesc::vertex(size, mesh.vertices.data(), "RenderGeometryVB"));
        if (!vb) return std::unexpected(vb.error());
        geo.vertexBuffer = std::move(*vb);
    }

    if (geo.indexCount > 0 && !mesh.indices.empty()) {
        uint32_t size = static_cast<uint32_t>(mesh.indices.size());
        auto ib = device.createBuffer(
            BufferDesc::index(size, mesh.indices.data(), "RenderGeometryIB"));
        if (!ib) return std::unexpected(ib.error());
        geo.indexBuffer = std::move(*ib);
    }

    geo.uploaded = true;
    return geo;
}

} // namespace mulan::engine
