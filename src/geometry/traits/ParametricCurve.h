/**
 * @file ParametricCurve.h
 * @brief 参数曲线特征接口
 *
 * 基于 truck-geotrait::traits::parametric_curve。
 * 定义参数曲线的求值、求导接口。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Export.h"
#include <optional>

namespace mulan::geometry {

/// 参数曲线抽象接口
/// @tparam P 点类型 (Point2/Point3)
/// @tparam V 向量类型 (Vector2/Vector3)
template<typename P, typename V>
class ParametricCurve {
public:
    virtual ~ParametricCurve() = default;

    /// 求值: 返回参数 t 处的点
    virtual P subs(double t) const = 0;

    /// 一阶导数
    virtual V der(double t) const = 0;

    /// 二阶导数
    virtual V der2(double t) const = 0;

    /// n 阶导数
    virtual V derN(size_t n, double t) const = 0;

    /// 多阶导数 (0~n)
    virtual CurveDers<V> ders(size_t n, double t) const {
        CurveDers<V> result(n);
        for (size_t i = 0; i <= n; ++i) {
            result[i] = derN(i, t);
        }
        result.max_order = n;
        return result;
    }

    /// 参数范围
    virtual ParameterRange parameterRange() const = 0;

    /// 周期 (若有)
    virtual std::optional<double> period() const { return std::nullopt; }
};

/// 有界参数曲线 (具有有限参数范围)
template<typename P, typename V>
class BoundedCurve : public ParametricCurve<P, V> {
public:
    /// 返回参数范围的 (min, max) 对
    virtual std::pair<double, double> rangeTuple() const = 0;

    /// 参数范围起点的曲线值
    P front() const { return this->subs(rangeTuple().first); }

    /// 参数范围终点的曲线值
    P back() const { return this->subs(rangeTuple().second); }

    /// 参数分割: 将参数区间自适应细分为多段 (用于渲染/网格化)
    /// @return (参数节点, 对应点) 对
    virtual std::pair<std::vector<double>, std::vector<P>>
        parameterDivision(std::pair<double, double> range, double tol) const = 0;

    /// 应用 4x4 变换矩阵
    virtual void transformBy(const Matrix4& mat) = 0;

    /// 返回变换后的副本 (调用方自己拷贝再变换)
};

} // namespace mulan::geometry
