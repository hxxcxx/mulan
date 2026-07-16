/**
 * @file geometry_draw_batch.cpp
 * @brief 对象实例批兼容键、连续 run 扫描与容量边界拆分实现。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include "geometry_draw_batch.h"

namespace mulan::engine {
namespace {

void appendRange(std::vector<GeometryDrawBatchRange>& out, size_t first, size_t count, bool instanced) {
    if (count == 0)
        return;
    if (!instanced && !out.empty() && !out.back().instanced && out.back().first + out.back().count == first) {
        out.back().count += count;
        return;
    }
    out.push_back({ .first = first, .count = count, .instanced = instanced });
}

void appendInstancedRun(std::vector<GeometryDrawBatchRange>& out, size_t first, size_t count) {
    constexpr size_t Capacity = kObjectBatchCapacity;
    const size_t fullBatchCount = count / Capacity;
    const size_t remainder = count % Capacity;
    size_t cursor = first;

    // 2..7 个尾项若逐条绘制会增加多个 draw；从最后一个满批次挪到 8 项尾批。
    // 单个尾项保留 legacy，可让前批保持满载且 draw 总数不变。
    if (fullBatchCount > 0 && remainder >= 2 && remainder < kObjectBatchMinSize) {
        for (size_t batch = 0; batch + 1 < fullBatchCount; ++batch) {
            appendRange(out, cursor, Capacity, true);
            cursor += Capacity;
        }
        const size_t adjustedLastBatch = Capacity - (kObjectBatchMinSize - remainder);
        appendRange(out, cursor, adjustedLastBatch, true);
        cursor += adjustedLastBatch;
        appendRange(out, cursor, kObjectBatchMinSize, true);
        return;
    }

    for (size_t batch = 0; batch < fullBatchCount; ++batch) {
        appendRange(out, cursor, Capacity, true);
        cursor += Capacity;
    }
    if (remainder >= kObjectBatchMinSize) {
        appendRange(out, cursor, remainder, true);
    } else {
        appendRange(out, cursor, remainder, false);
    }
}

}  // namespace

bool isGeometryDrawBatchCandidate(const MeshDrawCommand& command, const PipelineState* executorPipeline) noexcept {
    return command.batchInstancingEligible && command.visible && !command.translucent && command.instanceCount == 1 &&
           command.pipelineState && command.pipelineState == executorPipeline && command.vertexBuffer &&
           ((command.indexBuffer && command.indexCount > 0) || command.vertexCount > 0);
}

bool geometryDrawBatchCompatible(const MeshDrawCommand& lhs, const MeshDrawCommand& rhs) noexcept {
    return lhs.pipelineState == rhs.pipelineState && lhs.vertexBuffer == rhs.vertexBuffer &&
           lhs.indexBuffer == rhs.indexBuffer && lhs.indexCount == rhs.indexCount && lhs.indexType == rhs.indexType &&
           lhs.firstIndex == rhs.firstIndex && lhs.baseVertex == rhs.baseVertex && lhs.vertexCount == rhs.vertexCount &&
           lhs.topology == rhs.topology && lhs.materialIndex == rhs.materialIndex && lhs.albedoTex == rhs.albedoTex &&
           lhs.normalTex == rhs.normalTex && lhs.mrTex == rhs.mrTex && lhs.emissiveTex == rhs.emissiveTex &&
           lhs.aoTex == rhs.aoTex && lhs.sampler == rhs.sampler && lhs.isWire == rhs.isWire &&
           lhs.batchInstancingEligible == rhs.batchInstancingEligible;
}

void planGeometryDrawBatches(std::span<const MeshDrawCommand> commands, const PipelineState* executorPipeline,
                             bool instancingAvailable, std::vector<GeometryDrawBatchRange>& out) {
    out.clear();
    if (!instancingAvailable || !executorPipeline) {
        appendRange(out, 0, commands.size(), false);
        return;
    }

    size_t cursor = 0;
    while (cursor < commands.size()) {
        if (!isGeometryDrawBatchCandidate(commands[cursor], executorPipeline)) {
            appendRange(out, cursor, 1, false);
            ++cursor;
            continue;
        }

        size_t runEnd = cursor + 1;
        while (runEnd < commands.size() && isGeometryDrawBatchCandidate(commands[runEnd], executorPipeline) &&
               geometryDrawBatchCompatible(commands[cursor], commands[runEnd])) {
            ++runEnd;
        }
        const size_t runCount = runEnd - cursor;
        if (runCount < kObjectBatchMinSize) {
            appendRange(out, cursor, runCount, false);
        } else {
            appendInstancedRun(out, cursor, runCount);
        }
        cursor = runEnd;
    }
}

bool packGeometryDrawObjectBatch(std::span<const MeshDrawCommand> commands, ObjectBatchUniforms& out) noexcept {
    if (commands.empty() || commands.size() > kObjectBatchCapacity)
        return false;
    out = {};
    for (size_t index = 0; index < commands.size(); ++index) {
        const MeshDrawCommand& command = commands[index];
        out.objects[index] =
                makeObjectUniforms(command.worldTransform, command.pickId, command.selected, command.hovered);
    }
    return true;
}

}  // namespace mulan::engine
