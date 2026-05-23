/**
 * @file BSplineSurface.h
 * @brief B样条曲面
 *
 * 基于 truck-geometry::nurbs::BSplineSurface。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../BoundingBox.h"
#include "../traits/ParametricSurface.h"
#include "KnotVec.h"
#include "BSplineCurve.h"
#include "../Export.h"
#include <vector>
#include <utility>
#include <optional>

namespace MulanGeo::Geometry {

/// B样条曲面
/// @tparam P 控制点类型 (Point3/Vector4)
template<typename P>
class BSplineSurface
    : public ParametricSurface<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    BSplineSurface() = default;

    BSplineSurface(
        std::pair<KnotVec, KnotVec> knot_vecs,
        std::vector<std::vector<P>> controlPoints
    ) : knot_vecs_(std::move(knot_vecs))
        , control_points_(std::move(controlPoints)) {}

    static std::optional<BSplineSurface<P>> tryNew(
        std::pair<KnotVec, KnotVec> knot_vecs,
        std::vector<std::vector<P>> controlPoints
    ) {
        if (controlPoints.empty() || controlPoints[0].empty()) return std::nullopt;
        size_t udeg = knot_vecs.first.len() - controlPoints.size() - 1;
        size_t vdeg = knot_vecs.second.len() - controlPoints[0].size() - 1;
        if (soSmall(knot_vecs.first.rangeLength()) || soSmall(knot_vecs.second.rangeLength()))
            return std::nullopt;
        return BSplineSurface<P>(std::move(knot_vecs), std::move(controlPoints));
    }

    // --- 访问 ---

    const KnotVec& uKnotVec() const { return knot_vecs_.first; }
    const KnotVec& vKnotVec() const { return knot_vecs_.second; }
    const std::vector<std::vector<P>>& controlPoints() const { return control_points_; }

    std::pair<size_t, size_t> degree() const {
        size_t udeg = knot_vecs_.first.len() - control_points_.size() - 1;
        size_t vdeg = knot_vecs_.second.len() - control_points_[0].size() - 1;
        return {udeg, vdeg};
    }

    size_t uDegree() const { return knot_vecs_.first.len() - control_points_.size() - 1; }
    size_t vDegree() const {
        return control_points_.empty() ? 0 :
            knot_vecs_.second.len() - control_points_[0].size() - 1;
    }

    // --- ParametricSurface 接口 ---

    P subs(double u, double v) const override {
        auto [udeg, vdeg] = degree();
        size_t nu = control_points_.size();
        size_t nv = control_points_.empty() ? 0 : control_points_[0].size();

        // 计算 u 方向基函数
        auto u_basis = knot_vecs_.first.bsplineBasisFunctions(udeg, 0, u);
        // 计算 v 方向基函数
        auto v_basis = knot_vecs_.second.bsplineBasisFunctions(vdeg, 0, v);

        // 双重求和
        P result(0.0);
        auto u_slice = u_basis.as_slice();
        auto v_slice = v_basis.as_slice();

        for (size_t i = 0; i < u_slice.size(); ++i) {
            size_t ui = u_basis.base() + i;
            if (ui >= nu) continue;
            for (size_t j = 0; j < v_slice.size(); ++j) {
                size_t vj = v_basis.base() + j;
                if (vj >= nv) continue;
                result += control_points_[ui][vj] * (u_slice[i] * v_slice[j]);
            }
        }
        return result;
    }

    Diff uder(double u, double v) const override {
        auto [udeg, vdeg] = degree();
        auto u_basis1 = knot_vecs_.first.bsplineBasisFunctions(udeg, 1, u);
        auto v_basis = knot_vecs_.second.bsplineBasisFunctions(vdeg, 0, v);
        return compute_partial(u_basis1, v_basis);
    }

    Diff vder(double u, double v) const override {
        auto [udeg, vdeg] = degree();
        auto u_basis = knot_vecs_.first.bsplineBasisFunctions(udeg, 0, u);
        auto v_basis1 = knot_vecs_.second.bsplineBasisFunctions(vdeg, 1, v);
        return compute_partial(u_basis, v_basis1);
    }

    Diff uuder(double u, double v) const override {
        auto [udeg, vdeg] = degree();
        auto u_basis2 = knot_vecs_.first.bsplineBasisFunctions(udeg, 2, u);
        auto v_basis = knot_vecs_.second.bsplineBasisFunctions(vdeg, 0, v);
        return compute_partial(u_basis2, v_basis);
    }

    Diff uvder(double u, double v) const override {
        auto [udeg, vdeg] = degree();
        auto u_basis1 = knot_vecs_.first.bsplineBasisFunctions(udeg, 1, u);
        auto v_basis1 = knot_vecs_.second.bsplineBasisFunctions(vdeg, 1, v);
        return compute_partial(u_basis1, v_basis1);
    }

    Diff vvder(double u, double v) const override {
        auto [udeg, vdeg] = degree();
        auto u_basis = knot_vecs_.first.bsplineBasisFunctions(udeg, 0, u);
        auto v_basis2 = knot_vecs_.second.bsplineBasisFunctions(vdeg, 2, v);
        return compute_partial(u_basis, v_basis2);
    }

    Diff derMN(size_t m, size_t n, double u, double v) const override {
        auto [udeg, vdeg] = degree();
        auto u_basis = knot_vecs_.first.bsplineBasisFunctions(udeg, m, u);
        auto v_basis = knot_vecs_.second.bsplineBasisFunctions(vdeg, n, v);
        return compute_partial(u_basis, v_basis);
    }

    std::pair<ParameterRange, ParameterRange> parameterRange() const override {
        Bound u0{BoundKind::Included, knot_vecs_.first[0]};
        Bound u1{BoundKind::Included, knot_vecs_.first[knot_vecs_.first.len() - 1]};
        Bound v0{BoundKind::Included, knot_vecs_.second[0]};
        Bound v1{BoundKind::Included, knot_vecs_.second[knot_vecs_.second.len() - 1]};
        return {{u0, u1}, {v0, v1}};
    }

    // --- 变换 ---

    void transformBy(const Matrix4& trans) {
        for (auto& row : control_points_) {
            for (auto& cp : row) {
                cp = transform_point(trans, cp);
            }
        }
    }

    /// 变换点 (特化 Point3)
    static Point3 transform_point(const Matrix4& mat, const Point3& p) {
        auto v = mat * glm::dvec4(p, 1.0);
        return Point3(v);
    }

    /// 变换点 (特化 Vector4 - NURBS 齐次坐标)
    static Vector4 transform_point(const Matrix4& mat, const Vector4& p) {
        return mat * p;
    }

    /// u 方向参数曲线
    BSplineCurve<P> u_curve(double v) const {
        std::vector<P> cps;
        size_t nu = control_points_.size();
        size_t vdeg = vDegree();
        auto v_basis = knot_vecs_.second.bsplineBasisFunctions(vdeg, 0, v);
        auto v_slice = v_basis.as_slice();
        size_t nv = control_points_.empty() ? 0 : control_points_[0].size();

        for (size_t i = 0; i < nu; ++i) {
            P pt(0.0);
            for (size_t j = 0; j < v_slice.size(); ++j) {
                size_t vj = v_basis.base() + j;
                if (vj < nv) {
                    pt += control_points_[i][vj] * v_slice[j];
                }
            }
            cps.push_back(pt);
        }
        return BSplineCurve<P>(knot_vecs_.first, std::move(cps));
    }

    /// v 方向参数曲线
    BSplineCurve<P> v_curve(double u) const {
        size_t nu = control_points_.size();
        size_t nv = control_points_.empty() ? 0 : control_points_[0].size();
        size_t udeg = uDegree();
        auto u_basis = knot_vecs_.first.bsplineBasisFunctions(udeg, 0, u);
        auto u_slice = u_basis.as_slice();

        std::vector<P> cps;
        for (size_t j = 0; j < nv; ++j) {
            P pt(0.0);
            for (size_t i = 0; i < u_slice.size(); ++i) {
                size_t ui = u_basis.base() + i;
                if (ui < nu) {
                    pt += control_points_[ui][j] * u_slice[i];
                }
            }
            cps.push_back(pt);
        }
        return BSplineCurve<P>(knot_vecs_.second, std::move(cps));
    }

    /// 包围盒
    BoundingBox3D bounding_box() const
        requires std::same_as<P, Point3>
    {
        BoundingBox3D bb;
        for (const auto& row : control_points_) {
            for (const auto& cp : row) {
                bb.push(cp);
            }
        }
        return bb;
    }

private:
    std::pair<KnotVec, KnotVec> knot_vecs_;
    std::vector<std::vector<P>> control_points_;

    /// 双重求和辅助函数
    Diff compute_partial(const BasisWindow& u_basis, const BasisWindow& v_basis) const {
        size_t nu = control_points_.size();
        size_t nv = control_points_.empty() ? 0 : control_points_[0].size();
        auto u_slice = u_basis.as_slice();
        auto v_slice = v_basis.as_slice();

        P result(0.0);
        for (size_t i = 0; i < u_slice.size(); ++i) {
            size_t ui = u_basis.base() + i;
            if (ui >= nu) continue;
            for (size_t j = 0; j < v_slice.size(); ++j) {
                size_t vj = v_basis.base() + j;
                if (vj >= nv) continue;
                result += control_points_[ui][vj] * (u_slice[i] * v_slice[j]);
            }
        }
        return static_cast<Diff>(result);
    }
};

} // namespace MulanGeo::Geometry
