/**
 * @file BSplineCurve.h
 * @brief B样条曲线
 *
 * 基于 truck-geometry::nurbs::BSplineCurve。
 * 使用 de Boor 算法进行求值。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../BoundingBox.h"
#include "../traits/ParametricCurve.h"
#include "../traits/SearchParameter.h"
#include "KnotVec.h"
#include "../Export.h"
#include <vector>
#include <optional>
#include <stdexcept>
#include <algorithm>

namespace MulanGeo::Geometry {

/// B样条曲线
/// @tparam P 控制点类型 (Point2/Point3/Vector4)
template<typename P>
class BSplineCurve
    : public BoundedCurve<P, decltype(P{} - P{})> {
public:
    using Diff = decltype(P{} - P{});

    // --- 构造 ---

    BSplineCurve() = default;

    /// 构造 B样条曲线 (不检查有效性)
    BSplineCurve(KnotVec knotVec, std::vector<P> controlPoints)
        : knot_vec_(std::move(knotVec))
        , control_points_(std::move(controlPoints)) {}

    /// 构造并检查有效性
    static std::optional<BSplineCurve<P>> tryNew(
        KnotVec knotVec, std::vector<P> controlPoints
    ) {
        if (controlPoints.empty()) return std::nullopt;
        if (knotVec.len() <= controlPoints.size()) return std::nullopt;
        if (soSmall(knotVec.rangeLength())) return std::nullopt;
        return BSplineCurve<P>(std::move(knotVec), std::move(controlPoints));
    }

    /// 构造 (检查有效性, 失败抛异常)
    static BSplineCurve<P> newChecked(KnotVec knotVec, std::vector<P> controlPoints) {
        auto result = tryNew(std::move(knotVec), std::move(controlPoints));
        if (!result) throw std::runtime_error("BSplineCurve: invalid parameters");
        return *result;
    }

    // --- 访问 ---

    const KnotVec& knotVec() const { return knot_vec_; }
    const std::vector<P>& controlPoints() const { return control_points_; }
    size_t degree() const { return knot_vec_.len() - control_points_.size() - 1; }
    bool isClamped() const { return knot_vec_.isClamped(degree()); }

    // --- ParametricCurve 接口 ---

    P subs(double t) const override {
        size_t k = degree();
        size_t n = control_points_.size();

        if (n == 0) return P(0.0);

        // 处理边界: 使用 clamped 端点
        if (t <= knot_vec_[0] + TOLERANCE && knot_vec_.isClamped(degree())) {
            return control_points_.front();
        }
        if (t >= knot_vec_[knot_vec_.len() - 1] - TOLERANCE && knot_vec_.isClamped(degree())) {
            return control_points_.back();
        }

        // de Boor 算法
        // 找到 t 所在的节点区间 [knots[i], knots[i+1])
        size_t idx = find_span(t);

        // 计算非零基函数
        auto basis = knot_vec_.bsplineBasisFunctions(degree(), 0, t);

        // 加权求和
        P result(0.0);
        auto slice = basis.as_slice();
        for (size_t j = 0; j < slice.size(); ++j) {
            size_t cp_idx = basis.base() + j;
            if (cp_idx < n) {
                result += static_cast<P>(control_points_[cp_idx] * slice[j]);
            }
        }
        return result;
    }

    Diff der(double t) const override {
        return derivation_curve().subs(t);
    }

    Diff der2(double t) const override {
        return derivation_curve().derivation_curve().subs(t);
    }

    Diff derN(size_t n, double t) const override {
        auto curve = *this;
        for (size_t i = 0; i < n; ++i) {
            curve = curve.derivation_curve();
        }
        return curve.subs(t);
    }

    ParameterRange parameterRange() const override {
        Bound b0{BoundKind::Included, knot_vec_[0]};
        Bound b1;
        if (knot_vec_.isClamped(degree())) {
            b1 = Bound{BoundKind::Included, knot_vec_[knot_vec_.len() - 1]};
        } else {
            b1 = Bound{BoundKind::Excluded, knot_vec_[knot_vec_.len() - 1]};
        }
        return {b0, b1};
    }

    // --- BoundedCurve 接口 ---

    std::pair<double, double> rangeTuple() const override {
        return {knot_vec_[0], knot_vec_[knot_vec_.len() - 1]};
    }

    // --- 导数曲线 ---

    /// 返回导数 B样条曲线
    BSplineCurve<Diff> derivation_curve() const {
        size_t k = degree();
        size_t n = control_points_.size();

        if (k == 0) {
            // 零次曲线的导数是零
            std::vector<Diff> zeros(n, Diff(0.0));
            return BSplineCurve<Diff>(knot_vec_, std::move(zeros));
        }

        std::vector<Diff> new_points;
        new_points.reserve(n + 1);
        for (size_t i = 0; i <= n; ++i) {
            Diff delta;
            if (i == 0) {
                delta = control_points_[0] - P(0.0); // to_vec
            } else if (i == n) {
                delta = P(0.0) - control_points_[n - 1];
            } else {
                delta = control_points_[i] - control_points_[i - 1];
            }
            double denom = knot_vec_[i + k] - knot_vec_[i];
            double coef = static_cast<double>(k) * inv_or_zero(denom);
            new_points.push_back(delta * coef);
        }

        return BSplineCurve<Diff>(knot_vec_, std::move(new_points));
    }

    // --- BoundedCurve 接口 ---

    std::pair<std::vector<double>, std::vector<P>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        std::vector<double> params;
        std::vector<P> points;
        double t0 = range.first;
        double t1 = range.second;

        params.push_back(t0);
        points.push_back(subs(t0));

        adaptive_division(params, points, t0, t1, tol, 0);

        params.push_back(t1);
        points.push_back(subs(t1));

        return {params, points};
    }

    // --- 变换 ---

    void transformBy(const Matrix4& trans) override {
        for (auto& cp : control_points_) {
            cp = transform_point(trans, cp);
        }
    }

    // --- 方向反转 (Invertible) ---

    /// 反转曲线方向: subs(t) → subs(a + b - t)
    void invert() {
        std::reverse(control_points_.begin(), control_points_.end());
        double a = knot_vec_[0];
        double b = knot_vec_[knot_vec_.len() - 1];
        std::vector<double> new_knots;
        new_knots.reserve(knot_vec_.len());
        for (size_t i = 0; i < knot_vec_.len(); ++i) {
            new_knots.push_back(a + b - knot_vec_[knot_vec_.len() - 1 - i]);
        }
        knot_vec_ = KnotVec(std::move(new_knots));
    }

    /// 返回反转后的副本
    BSplineCurve inverse() const {
        BSplineCurve c(*this);
        c.invert();
        return c;
    }

    // --- 切割 ---

    /// 在参数 t 处切割曲线，返回 (左半段, 右半段)
    /// 使用节点插入使 t 处重复度 = degree+1，然后分割控制点/节点向量
    std::pair<BSplineCurve<P>, BSplineCurve<P>> cut(double t) const {
        size_t deg = degree();
        size_t n = control_points_.size();

        std::vector<double> knots = knot_vec_.as_vec();
        std::vector<P> cps = control_points_;

        // Boehm 节点插入: 使 t 处重复度 = degree+1
        // 找 t 所在节点跨 [s, s+1)
        size_t s = 0;
        for (size_t i = knots.size() - 1; i > 0; --i) {
            if (knots[i - 1] <= t + TOLERANCE) { s = i - 1; break; }
        }

        // 已有重复度
        size_t mult = 0;
        for (size_t i = 0; i < knots.size(); ++i) {
            if (near(knots[i], t)) ++mult;
            else if (knots[i] > t) break;
        }

        // 需要插入 (degree+1 - mult) 次
        size_t insertions = deg + 1 - mult;

        for (size_t r = 0; r < insertions; ++r) {
            // Boehm 插入一次节点 t
            // 找插入位置
            auto ins = std::upper_bound(knots.begin(), knots.end(), t);
            size_t k = static_cast<size_t>(ins - knots.begin());

            // 修改控制点
            size_t start = (k > deg + 1) ? k - deg - 1 : 0;
            size_t end = static_cast<size_t>((k < n + deg + 1 - mult) ? k - mult + r : n);

            for (size_t i = end; i > start; --i) {
                double alpha;
                size_t idx = i - 1;
                if (idx + deg + 1 < knots.size() && idx < knots.size()) {
                    double denom = knots[idx + deg + 1] - knots[idx];
                    alpha = soSmall(denom) ? 1.0 : (t - knots[idx]) / denom;
                } else {
                    alpha = 1.0;
                }
                if (idx < cps.size() && idx + 1 < cps.size()) {
                    cps[idx] = cps[idx] * (1.0 - alpha) + cps[idx + 1] * alpha;
                }
            }

            // 插入节点
            knots.insert(ins, t);
        }

        // 现在节点 t 处有 repetition deg+1, 找到它的起始位置
        size_t k_start = 0;
        for (size_t i = 0; i < knots.size(); ++i) {
            if (near(knots[i], t)) { k_start = i; break; }
        }

        // 控制点分割位置
        size_t cp_split = k_start - deg;

        // 构造左右两段
        std::vector<double> left_knots, right_knots;
        std::vector<P> left_cps, right_cps;

        for (size_t i = 0; i <= cp_split; ++i) left_cps.push_back(cps[i]);
        for (size_t i = cp_split; i < cps.size(); ++i) right_cps.push_back(cps[i]);

        // 左段: knots[0..k_start] + deg+1 copies of t
        for (size_t i = 0; i <= k_start; ++i) left_knots.push_back(knots[i]);
        for (size_t i = 0; i <= deg; ++i) left_knots.push_back(t);

        // 右段: deg+1 copies of t + knots[k_start..end]
        for (size_t i = 0; i <= deg; ++i) right_knots.push_back(t);
        for (size_t i = k_start; i < knots.size(); ++i) right_knots.push_back(knots[i]);

        return {
            BSplineCurve<P>(KnotVec(std::move(left_knots)), std::move(left_cps)),
            BSplineCurve<P>(KnotVec(std::move(right_knots)), std::move(right_cps))
        };
    }

    // --- 其他操作 ---

    void knot_normalize() { knot_vec_.normalize(); }
    void knot_translate(double offset) { knot_vec_.translate(offset); }

    /// 包围盒
    BoundingBox3D bounding_box() const
        requires std::same_as<P, Point3>
    {
        BoundingBox3D bb;
        for (const auto& cp : control_points_) {
            bb.push(cp);
        }
        return bb;
    }

private:
    KnotVec knot_vec_;
    std::vector<P> control_points_;

    /// 找到 t 所在的节点区间
    size_t find_span(double t) const {
        size_t n = control_points_.size();
        size_t deg = degree();

        // 二分查找
        size_t lo = deg;
        size_t hi = n;
        size_t mid = (lo + hi) / 2;

        while (t < knot_vec_[mid] || t >= knot_vec_[mid + 1]) {
            if (t < knot_vec_[mid]) {
                hi = mid;
            } else {
                lo = mid;
            }
            mid = (lo + hi) / 2;
            if (lo >= hi) break;
        }
        return mid;
    }

    /// 自适应分割
    void adaptive_division(
        std::vector<double>& params,
        std::vector<P>& points,
        double t0, double t1, double tol, int depth
    ) const {
        if (depth > 20) return;

        double t_mid = (t0 + t1) * 0.5;
        P p0 = subs(t0);
        P p1 = subs(t1);
        P p_mid = subs(t_mid);

        // 检查中点是否在连接线上
        P linear_mid = (p0 + p1) * 0.5;
        double dist = glm::length(p_mid - linear_mid);

        if (dist > tol) {
            adaptive_division(params, points, t0, t_mid, tol, depth + 1);
            params.push_back(t_mid);
            points.push_back(p_mid);
            adaptive_division(params, points, t_mid, t1, tol, depth + 1);
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
};

} // namespace MulanGeo::Geometry
