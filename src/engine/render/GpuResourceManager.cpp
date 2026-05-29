/**
 * @file GpuResourceManager.cpp
 * @brief GPU 资源管理器实现 — 真实 RHI Buffer 创建
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "GpuResourceManager.h"
#include "../rhi/Device.h"
#include "../rhi/Buffer.h"

namespace mulan::engine {

GpuResourceManager::GpuResourceManager(RHIDevice& device)
    : m_device(device) {
}

void GpuResourceManager::uploadFaceMesh(uint64_t key, const Mesh& mesh) {
    m_faceGeos[key] = createGpuBuffer(m_device, mesh);
}

void GpuResourceManager::uploadEdgeMesh(uint64_t key, const Mesh& mesh) {
    m_edgeGeos[key] = createGpuBuffer(m_device, mesh);
}

void GpuResourceManager::releaseResource(uint64_t key) {
    m_faceGeos.erase(key);
    m_edgeGeos.erase(key);
}

bool GpuResourceManager::hasResource(uint64_t key) const {
    auto it = m_faceGeos.find(key);
    if (it != m_faceGeos.end() && it->second.isValid()) return true;
    auto eit = m_edgeGeos.find(key);
    return eit != m_edgeGeos.end() && eit->second.isValid();
}

const GpuGeometry* GpuResourceManager::faceGeometry(uint64_t key) const {
    auto it = m_faceGeos.find(key);
    return it != m_faceGeos.end() && it->second.isValid() ? &it->second : nullptr;
}

const GpuGeometry* GpuResourceManager::edgeGeometry(uint64_t key) const {
    auto it = m_edgeGeos.find(key);
    return it != m_edgeGeos.end() && it->second.isValid() ? &it->second : nullptr;
}

void GpuResourceManager::clear() {
    m_faceGeos.clear();
    m_edgeGeos.clear();
}

GpuGeometry GpuResourceManager::createGpuBuffer(RHIDevice& device, const Mesh& mesh) {
    GpuGeometry geo;
    if (mesh.empty()) return geo;

    geo.vertexStride = mesh.vertexStride;
    geo.vertexCount  = mesh.vertexCount();
    geo.indexCount   = mesh.indexCount();

    if (geo.vertexCount > 0 && !mesh.vertices.empty()) {
        uint32_t size = static_cast<uint32_t>(mesh.vertices.size() * sizeof(float));
        geo.vertexBuffer = device.createBuffer(
            BufferDesc::vertex(size, mesh.vertices.data(), "WorldVB"));
    }

    if (geo.indexCount > 0 && !mesh.indices.empty()) {
        uint32_t size = static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));
        geo.indexBuffer = device.createBuffer(
            BufferDesc::index(size, mesh.indices.data(), "WorldIB"));
    }

    geo.uploaded = true;
    return geo;
}

} // namespace mulan::engine
