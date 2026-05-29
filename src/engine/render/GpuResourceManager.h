/**
 * @file GpuResourceManager.h
 * @brief GPU 资源管理器 — 集中管理 GpuGeometry，按 uint64_t key 索引
 *
 * 设计原则：
 * - 通用接口，不知道 Entity 或 world/ 领域概念
 * - key 为 uint64_t（对应 world::Entity::Id，但此处不作假设）
 * - 管理三角面 mesh / 边线 mesh 两种 GPU 资源
 * - 构造函数传入 RHIDevice，Buffer 创建时自动上传
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../geometry/Mesh.h"
#include "../render/RenderGeometry.h"

#include <cstdint>
#include <unordered_map>

namespace mulan::engine {

class RHIDevice;

class GpuResourceManager {
public:
    explicit GpuResourceManager(RHIDevice& device);

    void uploadFaceMesh(uint64_t key, const Mesh& mesh);
    void uploadEdgeMesh(uint64_t key, const Mesh& mesh);
    void releaseResource(uint64_t key);

    bool hasResource(uint64_t key) const;
    size_t resourceCount() const { return m_faceGeos.size() + m_edgeGeos.size(); }

    const GpuGeometry* faceGeometry(uint64_t key) const;
    const GpuGeometry* edgeGeometry(uint64_t key) const;

    void clear();

private:
    static GpuGeometry createGpuBuffer(RHIDevice& device, const Mesh& mesh);

    RHIDevice& m_device;
    std::unordered_map<uint64_t, GpuGeometry> m_faceGeos;
    std::unordered_map<uint64_t, GpuGeometry> m_edgeGeos;
};

} // namespace mulan::engine
