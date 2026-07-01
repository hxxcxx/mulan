/**
 * @file SubMesh.h
 * @brief SubMesh / EdgeSubMesh / LodLevel / MeshBatch — 渲染管线基础数据类型
 *
 * 对应 UE5 概念：
 *   SubMesh ≈ FMeshBatchElement（单个 index buffer range + materialId）
 *   MeshBatch ≈ FMeshBatch（同 pipeline 的 SubMesh 集合，合批中间态）
 *
 * @author hxxcxx
 * @date 2026-06-01
 */

#pragma once

#include "entity.h"

#include "mulan/engine/rhi/render_types.h"

#include <cstdint>
#include <vector>

namespace mulan::world {

/// 单个 SubMesh = index buffer 的一段 range + 独立材质
/// 对应 glTF 的一个 mesh primitive
struct SubMesh {
    uint32_t firstIndex    = 0;
    uint32_t indexCount    = 0;
    uint32_t vertexOffset  = 0;
    uint16_t materialId    = 0xFFFF;  // 0xFFFF = default
    engine::PrimitiveTopology topology = engine::PrimitiveTopology::TriangleList;
};

/// 边线 SubMesh（CAD 专用，不参与 LOD）
struct EdgeSubMesh {
    uint32_t firstIndex   = 0;
    uint32_t indexCount   = 0;
    uint32_t vertexOffset = 0;
};

/// 一个 LOD 级别 = 一组 SubMesh
struct LodLevel {
    std::vector<SubMesh> subMeshes;
    float screenSize = 1.0f;  // 该 LOD 的最小屏幕占比（future）
};

/// 合批 key — 相同 pipeline + 相同 material 才合并
struct PipelineKey {
    void*    pso        = nullptr;
    uint16_t materialId = 0xFFFF;

    bool operator==(const PipelineKey& o) const = default;
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey& k) const {
        return reinterpret_cast<size_t>(k.pso) ^ (static_cast<size_t>(k.materialId) << 16);
    }
};

/// 指向某个 Entity 的某个 SubMesh 的引用
struct SubMeshRef {
    Entity::Id entityId;
    int        subMeshIndex = 0;  // 0-based within LodLevel
};

/// 合批中间态 — 同 pipeline 的 SubMesh 集合
/// 产物：一个 instanced MeshDrawCommand（instanceCount = elements.size()）
struct MeshBatch {
    PipelineKey              key;
    std::vector<SubMeshRef>  elements;
};

} // namespace mulan::world
