/**
 * @file Processor.h
 * @brief 几何变换包装器
 *
 * 基于 truck-geometry::decorators::Processor。
 * 对任意几何对象施加 4x4 变换，委托完整的 ParametricCurve / ParametricSurface 接口。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Export.h"
#include <memory>
#include <utility>

namespace mulan::geometry {

/// 对几何对象施加变换的包装器
/// @tparam Geom 被包装的几何类型 (曲线或曲面)
/// @tparam Trans 变换类型 (Matrix4)
template<typename Geom, typename Trans = Matrix4>
class Processor {
public:
    Processor() = default;
    Processor(Geom geometry, Trans transform)
        : geometry_(std::move(geometry))
        , transform_(std::move(transform)) {}

    const Geom& geometry() const { return geometry_; }
    const Trans& transformation() const { return transform_; }

    // ================================================================
    // 通用委托 subs (曲线: subs(t); 曲面: subs(u,v))
    // ================================================================

    auto subs(auto... args) const {
        return applyTransform(geometry_.subs(args...));
    }

    // ================================================================
    // 曲线接口 (ParametricCurve)
    // ================================================================

    auto der(auto t) const { return applyTransformV(geometry_.der(t)); }
    auto der2(auto t) const { return applyTransformV(geometry_.der2(t)); }
    auto derN(size_t n, auto t) const { return applyTransformV(geometry_.derN(n, t)); }

    auto ders(size_t n, auto t) const {
        auto d = geometry_.ders(n, t);
        using V = decltype(applyTransformV(d[0]));
        CurveDers<V> result(n);
        for (size_t i = 0; i <= n; ++i) {
            result[i] = applyTransformV(d[i]);
        }
        return result;
    }

    auto parameterRange() const { return geometry_.parameterRange(); }
    auto period() const { return geometry_.period(); }

    // --- BoundedCurve 接口 ---

    auto rangeTuple() const { return geometry_.rangeTuple(); }
    auto front() const { return applyTransform(geometry_.front()); }
    auto back() const { return applyTransform(geometry_.back()); }

    auto parameterDivision(std::pair<double, double> range, double tol) const {
        auto [params, pts] = geometry_.parameterDivision(range, tol);
        std::vector<decltype(applyTransform(pts[0]))> tpts;
        tpts.reserve(pts.size());
        for (const auto& p : pts) {
            tpts.push_back(applyTransform(p));
        }
        return std::pair{std::move(params), std::move(tpts)};
    }

    // ================================================================
    // 曲面接口 (ParametricSurface)
    // ================================================================

    auto uder(auto u, auto v) const { return applyTransformV(geometry_.uder(u, v)); }
    auto vder(auto u, auto v) const { return applyTransformV(geometry_.vder(u, v)); }
    auto uuder(auto u, auto v) const { return applyTransformV(geometry_.uuder(u, v)); }
    auto uvder(auto u, auto v) const { return applyTransformV(geometry_.uvder(u, v)); }
    auto vvder(auto u, auto v) const { return applyTransformV(geometry_.vvder(u, v)); }
    auto derMN(size_t m, size_t n, auto u, auto v) const {
        return applyTransformV(geometry_.derMN(m, n, u, v));
    }

    auto surfaceParameterRange() const {
        return geometry_.parameterRange();
    }
    auto uPeriod() const { return geometry_.uPeriod(); }
    auto vPeriod() const { return geometry_.vPeriod(); }

    /// 法线 (使用逆转置矩阵，保证法线方向正确)
    auto normal(auto u, auto v) const {
        auto n = geometry_.normal(u, v);
        // 法线变换: 使用变换矩阵的逆转置
        auto inv_trans = glm::inverse(glm::transpose(glm::dmat3(transform_)));
        return Vector3(inv_trans * glm::dvec4(n, 0.0));
    }

    // ================================================================
    // 变换应用
    // ================================================================

    /// 对点应用变换 (位置向量, w=1)
    Point3 applyTransform(const Point3& p) const {
        auto v = transform_ * glm::dvec4(p, 1.0);
        return Point3(v);
    }

    Point2 applyTransform(const Point2& p) const {
        auto v = transform_ * glm::dvec4(p, 0.0, 1.0);
        return Point2(v);
    }

    /// 对向量应用变换 (方向向量, w=0)
    Vector3 applyTransformV(const Vector3& v) const {
        auto r = transform_ * glm::dvec4(v, 0.0);
        return Vector3(r);
    }

    Vector2 applyTransformV(const Vector2& v) const {
        auto r = transform_ * glm::dvec4(v, 0.0, 0.0);
        return Vector2(r);
    }

    Vector4 applyTransformV(const Vector4& v) const {
        return transform_ * v;
    }

    // ================================================================
    // 变换操作
    // ================================================================

    void transformBy(const Trans& trans) {
        transform_ = trans * transform_;
    }

    /// 返回变换后的副本
    Processor transformed(const Trans& trans) const {
        Processor c = *this;
        c.transformBy(trans);
        return c;
    }

    void invert() {
        transform_ = glm::inverse(transform_);
        if constexpr (requires { geometry_.invert(); }) {
            geometry_.invert();
        }
    }

    Processor inverse() const {
        Processor c = *this;
        c.invert();
        return c;
    }

private:
    Geom geometry_;
    Trans transform_;
};

} // namespace mulan::Geometry
