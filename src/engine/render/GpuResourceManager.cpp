/**
 * @file GpuResourceManager.cpp
 * @brief GPU 资源管理器实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "GpuResourceManager.h"

namespace mulan::engine {

void GpuResourceManager::uploadFaceMesh(uint64_t key, const Mesh& mesh) {
    // 后期接入：真实 GPU 上传（createBuffer + upload + bind）
    m_faceMeshes[key] = mesh;
}

void GpuResourceManager::uploadEdgeMesh(uint64_t key, const Mesh& mesh) {
    m_edgeMeshes[key] = mesh;
}

void GpuResourceManager::releaseResource(uint64_t key) {
    m_faceMeshes.erase(key);
    m_edgeMeshes.erase(key);
}

bool GpuResourceManager::hasResource(uint64_t key) const {
    return m_faceMeshes.count(key) > 0 || m_edgeMeshes.count(key) > 0;
}

const Mesh* GpuResourceManager::faceMesh(uint64_t key) const {
    auto it = m_faceMeshes.find(key);
    return it != m_faceMeshes.end() ? &it->second : nullptr;
}

const Mesh* GpuResourceManager::edgeMesh(uint64_t key) const {
    auto it = m_edgeMeshes.find(key);
    return it != m_edgeMeshes.end() ? &it->second : nullptr;
}

void GpuResourceManager::clear() {
    m_faceMeshes.clear();
    m_edgeMeshes.clear();
}

} // namespace mulan::engine
