/**
 * @file render_resource_cache.h
 * @brief RenderResourceCache 按资源 key 缓存渲染用 GPU 几何资源。
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

    void uploadFaceGeometry(uint64_t key, const Mesh& mesh);
    void uploadEdgeGeometry(uint64_t key, const Mesh& mesh);
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
