/**
 * @file mesh_attribute_generator.h
 * @brief 三角网格法线与切线空间属性的内部生成工具。
 * @author hxxcxx
 * @date 2026-07-17
 *
 * 本文件只暴露项目自有数据类型。MikkTSpace 等第三方接口被限制在对应的
 * 实现文件中，避免第三方类型渗透到 io 模块的公共边界。
 */
#pragma once

#include <mulan/math/math.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace mulan::io::detail {

/// 可直接提交给三角网格构建器的几何属性集合。
struct TriangleMeshData {
    std::vector<math::FVec3> positions;
    std::vector<math::FVec3> normals;
    std::vector<math::FVec2> texcoords;
    std::vector<math::FVec4> tangents;
    std::vector<uint32_t> indices;
};

enum class MeshAttributeError : uint8_t {
    InvalidVertexStreams,
    InvalidTriangleIndices,
    MeshTooLarge,
    TangentGenerationFailed,
    InvalidGeneratedTangents,
};

/// 返回适合日志和测试诊断的稳定错误文本。
[[nodiscard]] std::string_view meshAttributeErrorMessage(MeshAttributeError error) noexcept;

/// 校验三角列表、索引范围以及现有属性流的基本数值有效性。
[[nodiscard]] std::expected<void, MeshAttributeError> validateTriangleMesh(const TriangleMeshData& mesh);

/// 在缺少法线时按 glTF 规则生成平面法线；成功时会按面角拆分顶点并忽略旧切线。
[[nodiscard]] std::expected<void, MeshAttributeError> generateFlatNormals(TriangleMeshData& mesh);

/// 使用 MikkTSpace 生成切线，并仅在切线空间不连续处拆分共享顶点。
[[nodiscard]] std::expected<void, MeshAttributeError> generateMikkTangents(TriangleMeshData& mesh);

}  // namespace mulan::io::detail
