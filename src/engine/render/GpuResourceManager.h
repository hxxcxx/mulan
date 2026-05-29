/**
 * @file GpuResourceManager.h
 * @brief GPU 资源管理器 — 集中管理 GPU Buffer，按 uint64_t key 索引
 *
 * 设计原则：
 * - 通用接口，不知道 Entity 或 world/ 领域概念
 * - key 为 uint64_t（对应 world::Entity::Id，但此处不作假设）
 * - 管理三角面 mesh / 边线 mesh / 点云三种 GPU 资源
 * - 后期接入 RHI 做真实 GPU 上传
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../geometry/Mesh.h"

#include <cstdint>
#include <unordered_map>

namespace mulan::engine {

class GpuResourceManager {
public:
    GpuResourceManager() = default;
    ~GpuResourceManager() = default;

    /// 上传三角面 mesh
    void uploadFaceMesh(uint64_t key, const Mesh& mesh);

    /// 上传边线 mesh
    void uploadEdgeMesh(uint64_t key, const Mesh& mesh);

    /// 释放某个 key 的所有 GPU 资源
    void releaseResource(uint64_t key);

    /// 查询是否已有 GPU 资源
    bool hasResource(uint64_t key) const;

    /// 资源数量
    size_t resourceCount() const { return m_faceMeshes.size() + m_edgeMeshes.size(); }

    /// 按 key 获取缓存的三角面 mesh（CPU 侧，调试/验证用）
    const Mesh* faceMesh(uint64_t key) const;

    /// 按 key 获取缓存的边线 mesh
    const Mesh* edgeMesh(uint64_t key) const;

    /// 清空所有资源
    void clear();

private:
    std::unordered_map<uint64_t, Mesh> m_faceMeshes;
    std::unordered_map<uint64_t, Mesh> m_edgeMeshes;
};

} // namespace mulan::engine
