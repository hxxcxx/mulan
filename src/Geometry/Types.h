/**
 * @file Types.h
 * @brief B-rep 几何库基础类型定义
 *
 * 基于 truck-base 的 cgmath64 类型系统，使用 GLM 实现。
 * 所有点/向量使用 double 精度。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <vector>
#include <array>
#include <utility>
#include <optional>
#include <functional>

namespace MulanGeo::Geometry {

// ============================================================
// 2D 类型
// ============================================================
using Point2  = glm::dvec2;
using Vector2 = glm::dvec2;

// ============================================================
// 3D 类型
// ============================================================
using Point3  = glm::dvec3;
using Vector3 = glm::dvec3;

// ============================================================
// 4D 类型（齐次坐标, NURBS 用）
// ============================================================
using Vector4 = glm::dvec4;

// ============================================================
// 矩阵类型
// ============================================================
using Matrix3 = glm::dmat3;
using Matrix4 = glm::dmat4;

// ============================================================
// 参数范围边界
// ============================================================
enum class BoundKind : uint8_t {
    Included,   // 闭区间 [value
    Excluded,   // 开区间 (value
    Unbounded,  // 无界
};

struct Bound {
    BoundKind kind = BoundKind::Unbounded;
    double value = 0.0;
};

using ParameterRange = std::pair<Bound, Bound>;

// ============================================================
// 导数结果
// ============================================================
static constexpr size_t MAX_DER_ORDER = 10;

template<typename V>
struct CurveDers {
    std::array<V, MAX_DER_ORDER + 1> array{};
    size_t max_order = 0;

    CurveDers() = default;
    explicit CurveDers(size_t order) : max_order(order) {}

    V& operator[](size_t i) { return array[i]; }
    const V& operator[](size_t i) const { return array[i]; }

    size_t size() const { return max_order + 1; }

    CurveDers<V> der() const {
        CurveDers<V> result;
        result.max_order = max_order > 0 ? max_order - 1 : 0;
        for (size_t i = 0; i < result.size(); ++i) {
            result.array[i] = array[i + 1];
        }
        return result;
    }

    /// NURBS 有理曲线导数计算
    CurveDers<Vector3> ratDers() const
        requires std::same_as<V, Vector4>
    {
        CurveDers<Vector3> result(max_order);
        for (size_t i = 0; i <= max_order; ++i) {
            Vector3 sum(0.0);
            int c = 1;
            for (size_t j = 1; j < i; ++j) {
                c = c * static_cast<int>(i - j + 1) / static_cast<int>(j);
                sum += result[j] * array[i - j].w * static_cast<double>(c);
            }
            result[i] = (Vector3(array[i]) - sum) / array[0].w;
        }
        return result;
    }
};

template<typename V>
using SurfaceDers = std::vector<std::vector<V>>;

// ============================================================
// 几何错误类型
// ============================================================
enum class GeometryError {
    EmptyControlPoints,
    TooShortKnotVector,
    ZeroRange,
    TooLargeDegree,
    GaussianEliminationFailure,
    CannotEvaluate,
};

} // namespace MulanGeo::Geometry
