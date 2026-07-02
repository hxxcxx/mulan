/**
 * @file render_resource_cache.h
 * @brief RenderResourceCache —— 渲染资源缓存，按 uint64_t key 索引 GpuGeometry
 *
 * 设计原则：
 * - 缓存 CPU 侧 Mesh 对应的 GPU buffer（face / edge 两类几何）
 * - 不叫 Manager，不变成全局单例，不拥有文档资产
 * - key 为 uint64_t（对应 entity id，但此处不作领域假设）
 * - 构造函数传入 RHIDevice，Buffer 创建时自动上传
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "../geometry/mesh.h"
#include "../render/render_geometry.h"

#include <mulan/core/result/error.h>

#include <cstdint>
#include <expected>
#include <unordered_map>

namespace mulan::engine {

class RHIDevice;

class RenderResourceCache {
public:
    explicit RenderResourceCache(RHIDevice& device);

    void uploadFaceMesh(uint64_t key, const Mesh& mesh);
    void uploadEdgeMesh(uint64_t key, const Mesh& mesh);
    void releaseResource(uint64_t key);

    bool hasResource(uint64_t key) const;
    size_t resourceCount() const { return face_geos_.size() + edge_geos_.size(); }

    const GpuGeometry* faceGeometry(uint64_t key) const;
    const GpuGeometry* edgeGeometry(uint64_t key) const;

    void clear();

private:
    static std::expected<GpuGeometry, core::Error>
        createGpuBuffer(RHIDevice& device, const Mesh& mesh);

    RHIDevice& device_;
    std::unordered_map<uint64_t, GpuGeometry> face_geos_;
    std::unordered_map<uint64_t, GpuGeometry> edge_geos_;
};

} // namespace mulan::engine
