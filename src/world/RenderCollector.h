/**
 * @file RenderCollector.h
 * @brief 对标 BIMEngine GI 的绘制收集器 — 收集 mesh → 按材质合并 → 批量提交
 *
 * 设计原则：
 * - 不即调即画，收集→批量→提交
 * - 按 materialId 合并同材质实体的顶点到统一 Buffer
 * - 不负责 tessellate（GeometryData 已做）
 * - flush 交给下游（GPU Resource Manager）
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "Entity.h"

#include "mulan/engine/geometry/Mesh.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mulan::world {

class RenderCollector {
public:
    /// 合并后的批次数据，一次 DrawCall
    struct Batch {
        uint16_t      materialId = 0;
        engine::Mesh  mesh;              // 合并后的顶点 + 索引
        std::vector<engine::Mat4> transforms;  // per-instance world 变换（instancing 用）
        int           entityCount = 0;
        bool          isLines = false;   // edge/line 批次
    };

    /// 添加三角面 mesh
    void addMesh(Entity::Id id, const engine::Mesh& mesh, const engine::Mat4& world,
                 uint16_t materialId);

    /// 添加边线 mesh
    void addEdges(Entity::Id id, const engine::Mesh& edges, const engine::Mat4& world,
                  uint16_t materialId);

    /// 添加点云
    void addPoints(Entity::Id id, const engine::Mesh& points, const engine::Mat4& world,
                   uint16_t materialId);

    /// 查看已收集的批次
    const std::vector<Batch>& batches() const { return m_batches; }

    /// 获取某个 materialId 的批次索引
    int batchIndex(uint16_t materialId) const;

    /// 清空所有收集的数据
    void clear();

private:
    void mergeInto(Batch& batch, const engine::Mesh& mesh, const engine::Mat4& world);

    std::vector<Batch> m_batches;
    std::unordered_map<uint16_t, int> m_faceBatchIndex;    // materialId → index
    std::unordered_map<uint16_t, int> m_edgeBatchIndex;
    std::unordered_map<uint16_t, int> m_pointBatchIndex;
};

} // namespace mulan::world
