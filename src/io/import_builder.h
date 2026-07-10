/**
 * @file import_builder.h
 * @brief 标准网格构建工具:几何数据 → graphics::Mesh。
 * @author hxxcxx
 * @date 2026-07-03
 *
 * 本文件原含 ImportBuilder(Document 耦合的资产/实体创建器);导入层重构后
 * importer 改为产出中立 ParsedScene,Document 装载由 ParsedSceneLoader 统一完成,
 * ImportBuilder 已移除。仅保留 buildStandardMesh —— 它是纯几何工具,无 Document 耦合。
 */
#pragma once

#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace mulan::io {

/// 标准网格源数据(positions/normals/texcoords/tangents/indices 的 span 视图)。
struct StandardMeshSource {
    std::span<const math::FVec3> positions;
    std::span<const math::FVec3> normals;
    std::span<const math::FVec2> texcoords;
    std::span<const math::FVec4> tangents;
    std::span<const uint32_t> indices;
    graphics::PrimitiveTopology topology = graphics::PrimitiveTopology::TriangleList;
    bool force32BitIndices = false;
};

/// 把标准网格源数据构建为 graphics::Mesh(含顶点/索引缓冲 + 包围盒)。
graphics::Mesh buildStandardMesh(const StandardMeshSource& source);

}  // namespace mulan::io
