/**
 * @file BlendCurve.h
 * @brief G1/G2 连续过渡曲线
 *
 * 通过五次 Hermite 混合函数将两条曲线光滑连接。
 *
 * blend(t) = (1 - H(t)) * C0(t * L0) + H(t) * C1(t * L1)
 *
 * 其中 H(t) 是五次 Hermite 基函数:
 *   H(t)  = 6t^5 - 15t^4 + 10t^3
 *   H'(t) = 30t^4 - 60t^3 + 30t^2
 *   H''(t)= 120t^3 - 180t^2 + 60t
 *
 * 五次多项式保证 G2 连续性（位置 + 切线 + 曲率连续）。
 *
 * @author hxxcxx
 * @date 2026-05-28
 */
#pragma once

#include "../Types.h"
#include "../Tolerance.h"
#include "../traits/ParametricCurve.h"
#include "../Export.h"
#include <cmath>

namespace mulan::geometry {

/// G1/G2 过渡曲线
/// @tparam Curve0 起始曲线类型
/// @tparam Curve1 终止曲线类型
template<typename Curve0, typename Curve1>
class BlendCurve : public BoundedCurve<Point3, Vector3> {
public:
    BlendCurve() = default;

    /// @param c0 起始曲线
    /// @param c1 终止曲线
    /// @param range0 c0 的参数范围（用于映射 t∈[0,1] → c0 的参数）
    /// @param range1 c1 的参数范围
    BlendCurve(Curve0 c0, Curve1 c1,
               std::pair<double, double> range0,
               std::pair<double, double> range1)
        : c0_(std::move(c0))
        , c1_(std::move(c1))
        , r0_(range0)
        , r1_(range1) {}

    const Curve0& startCurve() const { return c0_; }
    const Curve1& endCurve() const { return c1_; }

    void setRange0(std::pair<double, double> r) { r0_ = r; }
    void setRange1(std::pair<double, double> r) { r1_ = r; }

    // --- 五次 Hermite 基函数及其导数 ---

    /// H(s) = 6s^5 - 15s^4 + 10s^3
    static double blendFunc(double s) {
        double s3 = s * s * s;
        double s4 = s3 * s;
        double s5 = s4 * s;
        return 6.0 * s5 - 15.0 * s4 + 10.0 * s3;
    }

    /// H'(s) = 30s^4 - 60s^3 + 30s^2
    static double blendFuncDer(double s) {
        double s2 = s * s;
        double s3 = s2 * s;
        double s4 = s3 * s;
        return 30.0 * s4 - 60.0 * s3 + 30.0 * s2;
    }

    /// H''(s) = 120s^3 - 180s^2 + 60s
    static double blendFuncDer2(double s) {
        double s2 = s * s;
        double s3 = s2 * s;
        return 120.0 * s3 - 180.0 * s2 + 60.0 * s;
    }

    /// H'''(s) = 360s^2 - 360s + 60
    static double blendFuncDer3(double s) {
        return 360.0 * s * s - 360.0 * s + 60.0;
    }

    /// H^(4)(s) = 720s - 360
    static double blendFuncDer4(double s) {
        return 720.0 * s - 360.0;
    }

    /// H^(5)(s) = 720
    static double blendFuncDer5(double /*s*/) {
        return 720.0;
    }

    /// H^(n)(s)
    static double blendFuncDerN(size_t n, double s) {
        switch (n) {
        case 0: return blendFunc(s);
        case 1: return blendFuncDer(s);
        case 2: return blendFuncDer2(s);
        case 3: return blendFuncDer3(s);
        case 4: return blendFuncDer4(s);
        case 5: return blendFuncDer5(s);
        default: return 0.0; // 五次多项式，六阶以上导数为零
        }
    }

    // --- ParametricCurve ---
    //
    // blend(t) = (1 - H(t)) * C0(t) + H(t) * C1(t)
    //   其中 C0(t) 实际求值在 c0_.subs(r0_.first + t * (r0_.second - r0_.first))
    //
    // 使用 Leibniz 规则:
    //   blend^(n)(t) = Σ_{k=0}^{n} C(n,k) * [(1-H)^(k) * C0^(n-k) + H^(k) * C1^(n-k)]
    //
    // 其中:
    //   (1-H)^(0) = 1-H, (1-H)^(k) = -H^(k) for k >= 1
    //   C0^(k)(t) = c0_.derN(k, r0(t)) * L0^k  (链式法则)
    //   C1^(k)(t) = c1_.derN(k, r1(t)) * L1^k

    Point3 subs(double t) const override {
        double h = blendFunc(t);
        Point3 p0 = c0_.subs(mapT0(t));
        Point3 p1 = c1_.subs(mapT1(t));
        return (1.0 - h) * p0 + h * p1;
    }

    Vector3 der(double t) const override {
        double h = blendFunc(t);
        double dh = blendFuncDer(t);
        double L0 = r0_.second - r0_.first;
        double L1 = r1_.second - r1_.first;

        Point3 p0 = c0_.subs(mapT0(t));
        Point3 p1 = c1_.subs(mapT1(t));
        Vector3 d0 = c0_.der(mapT0(t)) * L0;
        Vector3 d1 = c1_.der(mapT1(t)) * L1;

        return -dh * p0 + (1.0 - h) * d0 + dh * p1 + h * d1;
    }

    Vector3 der2(double t) const override {
        double h = blendFunc(t);
        double dh = blendFuncDer(t);
        double d2h = blendFuncDer2(t);
        double L0 = r0_.second - r0_.first;
        double L1 = r1_.second - r1_.first;
        double L0_2 = L0 * L0;
        double L1_2 = L1 * L1;

        Point3 p0 = c0_.subs(mapT0(t));
        Point3 p1 = c1_.subs(mapT1(t));
        Vector3 d0 = c0_.der(mapT0(t)) * L0;
        Vector3 d1 = c1_.der(mapT1(t)) * L1;
        Vector3 dd0 = c0_.der2(mapT0(t)) * L0_2;
        Vector3 dd1 = c1_.der2(mapT1(t)) * L1_2;

        // (1-H)'' = -H'', (1-H)' = -H'
        return -d2h * p0 - 2.0 * dh * d0 + (1.0 - h) * dd0
               + d2h * p1 + 2.0 * dh * d1 + h * dd1;
    }

    Vector3 derN(size_t n, double t) const override {
        if (n == 0) return subs(t) - Point3(0.0);
        if (n == 1) return der(t);
        if (n == 2) return der2(t);

        // 通用 Leibniz 公式:
        // blend^(n) = Σ_{k=0}^{n} C(n,k) * [(1-H)^(k) * C0^(n-k) + H^(k) * C1^(n-k)]
        double L0 = r0_.second - r0_.first;
        double L1 = r1_.second - r1_.first;

        Vector3 result(0.0);
        for (size_t k = 0; k <= n; ++k) {
            double cnk = binomial(n, k);

            // (1-H)^(k): k=0 时为 (1-H(t)), k>=1 时为 -H^(k)(t)
            double oneMinusH_k;
            if (k == 0) {
                oneMinusH_k = 1.0 - blendFunc(t);
            } else {
                oneMinusH_k = -blendFuncDerN(k, t);
            }

            double h_k = (k == 0) ? blendFunc(t) : blendFuncDerN(k, t);

            // C0^(n-k)(t) = c0.derN(n-k, mapT0(t)) * L0^(n-k)
            size_t order = n - k;
            double L0_pow = std::pow(L0, static_cast<double>(order));
            double L1_pow = std::pow(L1, static_cast<double>(order));

            Vector3 c0_deriv = c0_.derN(order, mapT0(t)) * L0_pow;
            Vector3 c1_deriv = c1_.derN(order, mapT1(t)) * L1_pow;

            result += cnk * (oneMinusH_k * c0_deriv + h_k * c1_deriv);
        }
        return result;
    }

    ParameterRange parameterRange() const override {
        return {{BoundKind::Included, 0.0}, {BoundKind::Included, 1.0}};
    }

    std::optional<double> period() const override { return std::nullopt; }
    std::pair<double, double> rangeTuple() const override { return {0.0, 1.0}; }

    std::pair<std::vector<double>, std::vector<Point3>>
    parameterDivision(std::pair<double, double> range, double tol) const override {
        double t0 = range.first, t1 = range.second;
        size_t n = std::max(size_t(16), static_cast<size_t>((t1 - t0) / tol * 0.1) + 1);
        n = std::min(n, size_t(10000));
        std::vector<double> params;
        std::vector<Point3> points;
        params.reserve(n + 1);
        points.reserve(n + 1);
        for (size_t i = 0; i <= n; ++i) {
            double t = t0 + (t1 - t0) * static_cast<double>(i) / static_cast<double>(n);
            params.push_back(t);
            points.push_back(subs(t));
        }
        return {params, points};
    }

    void transformBy(const Matrix4& mat) override {
        if constexpr (requires { c0_.transformBy(mat); }) {
            c0_.transformBy(mat);
        }
        if constexpr (requires { c1_.transformBy(mat); }) {
            c1_.transformBy(mat);
        }
    }

private:
    Curve0 c0_;
    Curve1 c1_;
    std::pair<double, double> r0_{0.0, 1.0};
    std::pair<double, double> r1_{0.0, 1.0};

    /// 将 t ∈ [0,1] 映射到 c0 的参数范围
    double mapT0(double t) const {
        return r0_.first + t * (r0_.second - r0_.first);
    }

    /// 将 t ∈ [0,1] 映射到 c1 的参数范围
    double mapT1(double t) const {
        return r1_.first + t * (r1_.second - r1_.first);
    }

    /// 二项式系数 C(n, k)
    static double binomial(size_t n, size_t k) {
        if (k > n) return 0.0;
        if (k == 0 || k == n) return 1.0;
        if (k > n - k) k = n - k;
        double result = 1.0;
        for (size_t i = 0; i < k; ++i) {
            result *= static_cast<double>(n - i);
            result /= static_cast<double>(i + 1);
        }
        return result;
    }
};

} // namespace mulan::geometry
