/**
 * @file nurbs_curve.h
 * @brief NURBS 曲线（2D / 3D）— 模板化
 * @author hxxcxx
 * @date 2026-07-05
 *
 * 数学定义（The NURBS Book §4.x）：
 *   C(u) = Σ_{i=0..n} N_{i,p}(u) · w_i · P_i  /  Σ_{i=0..n} N_{i,p}(u) · w_i
 *   P_i 控制点，w_i 权重，N_{i,p} B-spline 基函数。
 *
 *   齐次坐标求值：把 P_i 提升为齐次向量 (w_i·P_i, w_i)（3D 点 → 4D；
 *   2D 点 → 3D），在齐次空间跑 de Boor，最后做透视除法除以末分量。
 *
 * 与 B-spline 的关系：
 *   - 权重全为 1 时 NURBS 退化为 B-spline。
 *   - 权重 ≠ 1 时 NURBS 可精确表示二次曲线（圆/椭圆/抛物线/双曲线）。
 *
 * 类型设计：
 *   - 模板参数 Point（Point2 / Point3），Vec 由 Point-Point 自动推导。
 *   - 齐次向量类型通过内部 trait Homogeneous 类型映射：
 *       Point2 → Vec3  (wx, wy, w)
 *       Point3 → Vec4  (wx, wy, wz, w)
 *   - 求值返回 Point，导数返回 Vec。
 *
 * 参数域与边界：同 BSplineCurve（u clamp 到定义域，前置条件用 assert）。
 *
 * 算法来源：
 *   齐次 de Boor 求值  —— NURBS Book A3.1 应用于齐次控制点
 *   有理解析导数      —— NURBS Book §4.3 / A3.4：
 *                        C'(u) = (A'(u) - C(u)·W'(u)) / W(u)
 *                        其中 A(u) = Σ N·w·P（向量，齐次前 2/3 分量）
 *                              W(u) = Σ N·w   （标量，齐次末分量）
 *                        A'、W' 用 B-spline 解析导数（齐次 hodograph）
 *   节点插入(Boehm)  —— NURBS Book A5.1 在齐次空间做
 */
#pragma once

#include "../basis/bspline_basis.h"
#include "../geom/point.h"
#include "../linalg/vec3.h"
#include "../linalg/vec4.h"

#include <cassert>
#include <utility>
#include <vector>

namespace mulan::math {

// ============================================================
// NURBS 齐次坐标 trait：Point → Homogeneous (Vec)
// ============================================================

namespace detail {

template<typename Point> struct NurbsHomogeneous;
template<> struct NurbsHomogeneous<Point2> { using Type = Vec3; };
template<> struct NurbsHomogeneous<Point3> { using Type = Vec4; };

} // namespace detail

// ============================================================
// NURBSCurve —— 模板化（Point = Point2 | Point3）
// ============================================================

template<typename Point>
class NURBSCurveT {
public:
    using PointList  = std::vector<Point>;
    using KnotVector = std::vector<double>;
    using Vec        = std::decay_t<decltype(std::declval<Point>() - std::declval<Point>())>;
    using Homog      = typename detail::NurbsHomogeneous<Point>::Type; // Vec3 / Vec4

    // ---------- 构造 ----------

    /// 控制点 + 次数构造，权重默认 1（此时退化为 B-spline），clamped 节点向量。
    NURBSCurveT(int degree, PointList controlPoints)
        : degree_(degree)
        , control_points_(std::move(controlPoints))
        , weights_(control_points_.size(), 1.0)
        , knots_(clampedKnotVector(degree, static_cast<int>(control_points_.size()))) {
        validateInvariants();
    }

    /// 控制点 + 显式权重 + clamped 节点向量。
    NURBSCurveT(int degree, PointList controlPoints, std::vector<double> weights)
        : degree_(degree)
        , control_points_(std::move(controlPoints))
        , weights_(std::move(weights))
        , knots_(clampedKnotVector(degree, static_cast<int>(control_points_.size()))) {
        validateInvariants();
    }

    /// 完整构造（控制点 + 权重 + 显式节点向量）。
    NURBSCurveT(int degree, PointList controlPoints,
                std::vector<double> weights, KnotVector knots)
        : degree_(degree)
        , control_points_(std::move(controlPoints))
        , weights_(std::move(weights))
        , knots_(std::move(knots)) {
        validateInvariants();
    }

    // ---------- 查询 ----------

    int degree() const noexcept { return degree_; }
    int controlPointCount() const noexcept { return static_cast<int>(control_points_.size()); }
    int knotCount() const noexcept { return static_cast<int>(knots_.size()); }

    const PointList&    controlPoints() const noexcept { return control_points_; }
    const std::vector<double>& weights() const noexcept { return weights_; }
    const KnotVector&   knots()         const noexcept { return knots_; }

    std::pair<double, double> domain() const noexcept {
        const int m = static_cast<int>(knots_.size()) - 1;
        return { knots_[degree_], knots_[m - degree_] };
    }

    bool isValid() const noexcept {
        return degree_ >= 1
            && static_cast<int>(control_points_.size()) > degree_
            && static_cast<int>(weights_.size()) == controlPointCount()
            && static_cast<int>(knots_.size()) == controlPointCount() + degree_ + 1;
    }

    // ---------- 求值 ----------

    /// 齐次 de Boor 求值 + 透视除法。u 自动 clamp 到定义域。
    Point evaluate(double u) const {
        const auto [umin, umax] = domain();
        u = clampToDomain(u, umin, umax);

        Homog h = deBoorHomogeneous(u);
        double w = homogeneousW(h);
        assert(std::abs(w) > Tolerance::defaultValue().paramEps &&
               "NURBSCurve: degenerate weight (perspective divide by ~0)");
        return perspectiveDivide(h, w);
    }

    /// 有理解析一阶导数（NURBS Book §4.3）。
    /// 公式：C'(u) = (A'(u) - C(u)·W'(u)) / W(u)
    Vec derivative(double u) const {
        const auto [umin, umax] = domain();
        u = clampToDomain(u, umin, umax);

        const double eps = Tolerance::defaultValue().paramEps;

        // 当前点的齐次值 A(u)、W(u)
        Homog h = deBoorHomogeneous(u);
        double w = homogeneousW(h);
        assert(std::abs(w) > eps && "NURBSCurve::derivative: degenerate weight");
        Point c = perspectiveDivide(h, w); // C(u)

        // 齐次 hodograph：构造齐次控制点的导数曲线，求 A'、W'
        // 导数曲线次数 p-1，控制点 Q_i = p·(H_{i+1}-H_i)/(U_{i+p+1}-U_{i+1})（H 为齐次）
        std::vector<Homog> H = homogeneousControlPoints();
        std::vector<Homog> Q;
        Q.reserve(H.size() - 1);
        for (size_t i = 0; i + 1 < H.size(); ++i) {
            double denom = knots_[i + degree_ + 1] - knots_[i + 1];
            double scale = std::abs(denom) > eps
                         ? static_cast<double>(degree_) / denom : 0.0;
            Q.push_back(scale * homogeneousSub(H[i + 1], H[i]));
        }
        // 导数曲线节点向量：去首末各一
        KnotVector Uk;
        Uk.reserve(knots_.size() - 2);
        for (size_t i = 1; i + 1 < knots_.size(); ++i) Uk.push_back(knots_[i]);

        // 在导数曲线上 de Boor 求值得 (A'(u), W'(u))
        Homog hp = deBoorGeneric(Q, Uk, degree_ - 1, u);
        Vec   aPrime = homogeneousVec(hp);   // A'(u)（前 2/3 分量）
        double wPrime = homogeneousW(hp);    // W'(u)

        // C'(u) = (A'(u) - C(u)·W'(u)) / W(u)
        return (aPrime - c.asVec() * wPrime) * (1.0 / w);
    }

    // ---------- 结构操作 ----------

    /// 节点插入（Boehm，齐次空间）。原节点已存在时按当前重数正确收缩。
    void insertKnot(double u, int multiplicity = 1) {
        assert(multiplicity >= 1 && "insertKnot: multiplicity must be >= 1");
        const auto [umin, umax] = domain();
        u = clampToDomain(u, umin, umax);

        const int p = degree_;
        const double eps = Tolerance::defaultValue().paramEps;

        for (int m = 0; m < multiplicity; ++m) {
            const int n = controlPointCount() - 1;
            const int k = bsplineFindSpan(n, p, u, knots_);
            int s = 0;
            for (double knot : knots_) {
                if (std::abs(knot - u) <= eps) ++s;
            }

            PointList nwP;          std::vector<double> nwW;
            nwP.reserve(control_points_.size() + 1);
            nwW.reserve(weights_.size() + 1);

            // 在齐次空间做仿射组合：新 H = (1-a)·H_{i-1} + a·H_i，
            // 然后透视除法拆回 (P, w)
            auto pushRaw = [&](int i) {
                nwP.push_back(control_points_[i]);
                nwW.push_back(weights_[i]);
            };
            auto pushLerped = [&](int i, double a) {
                // 齐次插值：H_new = (1-a)·(w1·P1, w1) + a·(w2·P2, w2)
                double w1 = weights_[i - 1], w2 = weights_[i];
                double wNew = (1.0 - a) * w1 + a * w2;
                Point pNew;
                if (std::abs(wNew) > eps) {
                    Vec vh = (control_points_[i - 1].asVec() * ((1.0 - a) * w1)
                              + control_points_[i].asVec() * (a * w2)) * (1.0 / wNew);
                    pNew = Point::origin() + vh;
                } else {
                    pNew = mulan::math::lerp(control_points_[i - 1], control_points_[i], a);
                }
                nwP.push_back(pNew);
                nwW.push_back(wNew);
            };

            for (int i = 0; i <= k - p; ++i) pushRaw(i);
            for (int i = k - p + 1; i <= k - s; ++i) {
                double denom = knots_[i + p] - knots_[i];
                double a = std::abs(denom) > eps ? (u - knots_[i]) / denom : 0.0;
                pushLerped(i, a);
            }
            for (int i = k - s; i <= n; ++i) pushRaw(i);

            knots_.insert(knots_.begin() + k + 1, u);
            control_points_ = std::move(nwP);
            weights_ = std::move(nwW);
        }
    }

private:
    int                 degree_;
    PointList           control_points_;
    std::vector<double> weights_;
    KnotVector          knots_;

    void validateInvariants() const {
        assert(degree_ >= 1 && "NURBSCurve: degree must be >= 1");
        assert(controlPointCount() > degree_ &&
               "NURBSCurve: control point count must be > degree");
        assert(static_cast<int>(weights_.size()) == controlPointCount() &&
               "NURBSCurve: weight count must equal control point count");
        assert(static_cast<int>(knots_.size()) == controlPointCount() + degree_ + 1 &&
               "NURBSCurve: knot count must equal controlPointCount + degree + 1");
    }

    static double clampToDomain(double u, double lo, double hi) noexcept {
        return u < lo ? lo : (u > hi ? hi : u);
    }

    // ---- 齐次坐标的 Point / Vec 分量访问（屏蔽 Vec3/Vec4 差异）----

    static double homogeneousW(const Homog& h) noexcept;
    static Vec    homogeneousVec(const Homog& h) noexcept;
    static Point  perspectiveDivide(const Homog& h, double w) noexcept;
    static Homog  homogeneousSub(const Homog& a, const Homog& b) noexcept;

    /// 把控制点 + 权重提升为齐次坐标序列
    std::vector<Homog> homogeneousControlPoints() const {
        std::vector<Homog> H(control_points_.size());
        for (size_t i = 0; i < control_points_.size(); ++i) {
            H[i] = liftHomogeneous(control_points_[i], weights_[i]);
        }
        return H;
    }

    static Homog liftHomogeneous(const Point& p, double w) noexcept;

    // ---- 通用 de Boor（齐次 Vec3/Vec4 控制点）----
    static Homog deBoorGeneric(const std::vector<Homog>& cp, const KnotVector& U,
                               int p, double u) {
        const int n = static_cast<int>(cp.size()) - 1;
        const int k = bsplineFindSpan(n, p, u, U);
        std::vector<Homog> d(p + 1);
        for (int j = 0; j <= p; ++j) d[j] = cp[k - p + j];
        for (int r = 1; r <= p; ++r) {
            for (int j = p; j >= r; --j) {
                const int idx = k - p + j;
                double denom = U[idx + p + 1 - r] - U[idx];
                double a = 0.0;
                if (std::abs(denom) > Tolerance::defaultValue().paramEps) {
                    a = (u - U[idx]) / denom;
                }
                d[j] = homogeneousLerp(d[j - 1], d[j], a);
            }
        }
        return d[p];
    }

    /// 齐次 de Boor 求值（用本曲线控制点）
    Homog deBoorHomogeneous(double u) const {
        return deBoorGeneric(homogeneousControlPoints(), knots_, degree_, u);
    }

    static Homog homogeneousLerp(const Homog& a, const Homog& b, double t) noexcept;
};

// ============================================================
// 齐次坐标访问的 Point2 → Vec3 特化
// ============================================================

template<>
inline double NURBSCurveT<Point2>::homogeneousW(const Vec3& h) noexcept { return h.z; }

template<>
inline Vec2 NURBSCurveT<Point2>::homogeneousVec(const Vec3& h) noexcept {
    return Vec2(h.x, h.y);
}

template<>
inline Point2 NURBSCurveT<Point2>::perspectiveDivide(const Vec3& h, double w) noexcept {
    return Point2(h.x / w, h.y / w);
}

template<>
inline Vec3 NURBSCurveT<Point2>::homogeneousSub(const Vec3& a, const Vec3& b) noexcept {
    return a - b;
}

template<>
inline Vec3 NURBSCurveT<Point2>::liftHomogeneous(const Point2& p, double w) noexcept {
    return Vec3(p.x * w, p.y * w, w);
}

template<>
inline Vec3 NURBSCurveT<Point2>::homogeneousLerp(const Vec3& a, const Vec3& b, double t) noexcept {
    return a + (b - a) * t;
}

// ============================================================
// 齐次坐标访问的 Point3 → Vec4 特化
// ============================================================

template<>
inline double NURBSCurveT<Point3>::homogeneousW(const Vec4& h) noexcept { return h.w; }

template<>
inline Vec3 NURBSCurveT<Point3>::homogeneousVec(const Vec4& h) noexcept {
    return Vec3(h.x, h.y, h.z);
}

template<>
inline Point3 NURBSCurveT<Point3>::perspectiveDivide(const Vec4& h, double w) noexcept {
    return Point3(h.x / w, h.y / w, h.z / w);
}

template<>
inline Vec4 NURBSCurveT<Point3>::homogeneousSub(const Vec4& a, const Vec4& b) noexcept {
    return a - b;
}

template<>
inline Vec4 NURBSCurveT<Point3>::liftHomogeneous(const Point3& p, double w) noexcept {
    return Vec4(p.x * w, p.y * w, p.z * w, w);
}

template<>
inline Vec4 NURBSCurveT<Point3>::homogeneousLerp(const Vec4& a, const Vec4& b, double t) noexcept {
    return a + (b - a) * t;
}

// ============================================================
// 别名
// ============================================================

using NURBSCurve2d = NURBSCurveT<Point2>;
using NURBSCurve3d = NURBSCurveT<Point3>;

} // namespace mulan::math
