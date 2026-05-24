/**
 * @file SearchParameter.h
 * @brief 参数反求（点→参数）特征接口
 *
 * 基于 truck-geotrait::traits::searchParameter。
 * 给定空间点，反求对应的参数值。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../Export.h"
#include <optional>
#include <utility>

namespace MulanGeo::geometry {

// ============================================================
// 维度标记
// ============================================================
struct D1 {
    static constexpr size_t DIM = 1;
    using Parameter = double;
};

struct D2 {
    static constexpr size_t DIM = 2;
    using Parameter = std::pair<double, double>;
};

// ============================================================
// 搜索提示
// ============================================================
enum class SPHint1DKind : uint8_t { Parameter, Range, None };

struct SPHint1D {
    SPHint1DKind kind = SPHint1DKind::None;
    double parameter = 0.0;
    std::pair<double, double> range = {0.0, 0.0};
};

enum class SPHint2DKind : uint8_t { Parameter, Range, None };

struct SPHint2D {
    SPHint2DKind kind = SPHint2DKind::None;
    std::pair<double, double> parameter = {0.0, 0.0};
    std::pair<std::pair<double, double>, std::pair<double, double>> range = {};
};

// ============================================================
// 参数反求接口
// ============================================================

/// 1D 参数搜索：给定空间点，反求参数 t 使得 curve(t) = point
template<typename P>
class SearchParameter1D {
public:
    virtual ~SearchParameter1D() = default;
    virtual std::optional<double> searchParameter(
        const P& point, const SPHint1D& hint, size_t trials = SEARCH_PARAMETER_TRIALS) const = 0;
};

/// 1D 最近参数搜索：给定空间点，求使 |curve(t) - point| 最小的 t
template<typename P>
class SearchNearestParameter1D {
public:
    virtual ~SearchNearestParameter1D() = default;
    virtual std::optional<double> searchNearestParameter(
        const P& point, const SPHint1D& hint, size_t trials = SEARCH_PARAMETER_TRIALS) const = 0;
};

/// 2D 参数搜索：给定空间点，反求参数 (u,v) 使得 surface(u,v) = point
template<typename P>
class SearchParameter2D {
public:
    virtual ~SearchParameter2D() = default;
    virtual std::optional<std::pair<double, double>> searchParameter(
        const P& point, const SPHint2D& hint, size_t trials = SEARCH_PARAMETER_TRIALS) const = 0;
};

/// 2D 最近参数搜索
template<typename P>
class SearchNearestParameter2D {
public:
    virtual ~SearchNearestParameter2D() = default;
    virtual std::optional<std::pair<double, double>> searchNearestParameter(
        const P& point, const SPHint2D& hint, size_t trials = SEARCH_PARAMETER_TRIALS) const = 0;
};

} // namespace MulanGeo::Geometry
