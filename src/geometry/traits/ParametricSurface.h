/**
 * @file ParametricSurface.h
 * @brief 参数曲面特征接口
 *
 * 基于 truck-geotrait::traits::parametric_surface。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Export.h"
#include <optional>
#include <utility>

namespace mulan::geometry {

/// 参数曲面抽象接口
template<typename P, typename V>
class ParametricSurface {
public:
    virtual ~ParametricSurface() = default;

    /// 求值: 返回参数 (u, v) 处的点
    virtual P subs(double u, double v) const = 0;

    /// u 方向一阶偏导
    virtual V uder(double u, double v) const = 0;
    /// v 方向一阶偏导
    virtual V vder(double u, double v) const = 0;

    /// uu 二阶偏导
    virtual V uuder(double u, double v) const = 0;
    /// uv 混合二阶偏导
    virtual V uvder(double u, double v) const = 0;
    /// vv 二阶偏导
    virtual V vvder(double u, double v) const = 0;

    /// (m, n) 阶偏导
    virtual V derMN(size_t m, size_t n, double u, double v) const = 0;

    /// 多阶偏导矩阵
    virtual SurfaceDers<V> ders(size_t max_order, double u, double v) const {
        SurfaceDers<V> result;
        result.reserve(max_order + 1);
        for (size_t i = 0; i <= max_order; ++i) {
            std::vector<V> row;
            row.reserve(i + 1);
            for (size_t j = 0; j <= i; ++j) {
                row.push_back(derMN(i - j, j, u, v));
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    /// 参数范围 (u_range, v_range)
    virtual std::pair<ParameterRange, ParameterRange> parameterRange() const = 0;

    /// u 方向周期
    virtual std::optional<double> uPeriod() const { return std::nullopt; }
    /// v 方向周期
    virtual std::optional<double> vPeriod() const { return std::nullopt; }
};

/// 3D 参数曲面（带法线计算）
class GEOMETRY_API ParametricSurface3D : public ParametricSurface<Point3, Vector3> {
public:
    /// 法线向量
    Vector3 normal(double u, double v) const {
        Vector3 du = uder(u, v);
        Vector3 dv = vder(u, v);
        return glm::normalize(glm::cross(du, dv));
    }

    /// 法线 u 方向导数
    Vector3 normalUDer(double u, double v) const {
        Vector3 du = uder(u, v);
        Vector3 dv = vder(u, v);
        Vector3 duu = uuder(u, v);
        Vector3 duv = uvder(u, v);
        Vector3 n = glm::cross(du, dv);
        double len = glm::length(n);
        if (soSmall(len)) return Vector3(0.0);
        Vector3 nn = n / len;
        return (glm::cross(duu, dv) + glm::cross(du, duv) - nn * glm::dot(nn, glm::cross(duu, dv) + glm::cross(du, duv))) / len;
    }

    /// 法线 v 方向导数
    Vector3 normalVDer(double u, double v) const {
        Vector3 du = uder(u, v);
        Vector3 dv = vder(u, v);
        Vector3 duv = uvder(u, v);
        Vector3 dvv = vvder(u, v);
        Vector3 n = glm::cross(du, dv);
        double len = glm::length(n);
        if (soSmall(len)) return Vector3(0.0);
        Vector3 nn = n / len;
        return (glm::cross(duv, dv) + glm::cross(du, dvv) - nn * glm::dot(nn, glm::cross(duv, dv) + glm::cross(du, dvv))) / len;
    }

    /// 应用 4x4 变换矩阵
    virtual void transformBy(const Matrix4& mat) = 0;
};

} // namespace mulan::Geometry
