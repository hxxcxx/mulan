/**
 * @file RenderCollector.cpp
 * @brief 绘制收集器实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "RenderCollector.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace mulan::world {

static constexpr int FLOAT_PER_VERT = 8;  // pos3 + normal3 + tex2

int RenderCollector::batchIndex(uint16_t materialId) const {
    auto it = m_faceBatchIndex.find(materialId);
    return it != m_faceBatchIndex.end() ? it->second : -1;
}

void RenderCollector::addMesh(Entity::Id /*id*/, const engine::Mesh& mesh,
                               const engine::Mat4& world, uint16_t materialId) {
    if (mesh.empty()) return;

    auto it = m_faceBatchIndex.find(materialId);
    if (it == m_faceBatchIndex.end()) {
        Batch b;
        b.materialId = materialId;
        b.isLines = false;
        int idx = static_cast<int>(m_batches.size());
        m_batches.push_back(std::move(b));
        it = m_faceBatchIndex.insert({materialId, idx}).first;
    }
    mergeInto(m_batches[it->second], mesh, world);
    ++m_batches[it->second].entityCount;
}

void RenderCollector::addEdges(Entity::Id /*id*/, const engine::Mesh& edges,
                                const engine::Mat4& world, uint16_t materialId) {
    if (edges.empty()) return;

    auto it = m_edgeBatchIndex.find(materialId);
    if (it == m_edgeBatchIndex.end()) {
        Batch b;
        b.materialId = materialId;
        b.isLines = true;
        b.mesh.topology = engine::PrimitiveTopology::LineList;
        b.mesh.vertexStride = sizeof(float) * FLOAT_PER_VERT;
        int idx = static_cast<int>(m_batches.size());
        m_batches.push_back(std::move(b));
        it = m_edgeBatchIndex.insert({materialId, idx}).first;
    }
    mergeInto(m_batches[it->second], edges, world);
    ++m_batches[it->second].entityCount;
}

void RenderCollector::addPoints(Entity::Id /*id*/, const engine::Mesh& points,
                                 const engine::Mat4& world, uint16_t materialId) {
    if (points.empty()) return;

    auto it = m_pointBatchIndex.find(materialId);
    if (it == m_pointBatchIndex.end()) {
        Batch b;
        b.materialId = materialId;
        b.isLines = false;
        b.mesh.topology = engine::PrimitiveTopology::PointList;
        b.mesh.vertexStride = sizeof(float) * FLOAT_PER_VERT;
        int idx = static_cast<int>(m_batches.size());
        m_batches.push_back(std::move(b));
        it = m_pointBatchIndex.insert({materialId, idx}).first;
    }
    mergeInto(m_batches[it->second], points, world);
    ++m_batches[it->second].entityCount;
}

void RenderCollector::mergeInto(Batch& batch, const engine::Mesh& mesh,
                                 const engine::Mat4& world) {
    uint32_t baseVertex = batch.mesh.vertexCount();

    // 计算法线变换矩阵 (inverse transpose of upper 3×3)
    glm::dmat3 world33 = glm::dmat3(world);
    glm::dmat3 normalMatD = glm::inverseTranspose(world33);
    glm::mat3 normalMat = glm::mat3(normalMatD);

    glm::dmat4 w = glm::dmat4(world);
    glm::mat4 worldF = glm::mat4(w);

    size_t vertCount = mesh.vertexCount();
    const float* srcData = mesh.vertices.data();

    for (size_t v = 0; v < vertCount; ++v) {
        size_t offset = v * FLOAT_PER_VERT;
        glm::vec4 pos(srcData[offset], srcData[offset+1], srcData[offset+2], 1.0f);
        glm::vec3 nml(srcData[offset+3], srcData[offset+4], srcData[offset+5]);
        float u = srcData[offset+6];
        float vv = srcData[offset+7];

        glm::vec4 wp = worldF * pos;
        glm::vec3 wn = glm::normalize(normalMat * nml);

        batch.mesh.vertices.insert(batch.mesh.vertices.end(), {
            wp.x, wp.y, wp.z, wn.x, wn.y, wn.z, u, vv});
    }

    for (uint32_t idx : mesh.indices) {
        batch.mesh.indices.push_back(baseVertex + idx);
    }

    batch.transforms.push_back(world);

    if (batch.mesh.vertexStride == 0)
        batch.mesh.vertexStride = sizeof(float) * FLOAT_PER_VERT;
}

void RenderCollector::clear() {
    m_batches.clear();
    m_faceBatchIndex.clear();
    m_edgeBatchIndex.clear();
    m_pointBatchIndex.clear();
}

} // namespace mulan::world
