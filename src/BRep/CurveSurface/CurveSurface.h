/**
 * @file CurveSurface.h
 * @brief Curve / Surface variant 类型定义
 *
 * 定义 BRep 模块使用的具体曲线和曲面变体类型。
 * Curve 含递归变体 IntersectionCurve（通过 unique_ptr 打破循环）。
 *
 * 基于 truck-modeling::geometry::{Curve, Surface}。
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "../BRepExport.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/traits/ParametricCurve.h>
#include <MulanGeo/Geometry/traits/ParametricSurface.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>
#include <MulanGeo/Geometry/Nurbs/BSplineSurface.h>
#include <MulanGeo/Geometry/Nurbs/NurbsCurve.h>
#include <MulanGeo/Geometry/Nurbs/NurbsSurface.h>
#include <MulanGeo/Geometry/Decorators/RevolutedCurve.h>
#include <MulanGeo/Geometry/Decorators/ExtrudedCurve.h>
#include <MulanGeo/Geometry/Decorators/Processor.h>

#include <variant>
#include <memory>
#include <utility>

namespace MulanGeo::BRep {

// ============================================================
// 前向声明
// ============================================================

class Curve;
class Surface;

// ============================================================
// IntersectionCurve — 两曲面交线（递归变体）
// ============================================================

/// 两曲面交线
/// leader 为引导曲线（交线的近似），surface0/1 为交线所在的两张曲面。
/// 通过 unique_ptr<Curve> 打破与 Curve variant 的循环引用。
struct IntersectionCurve {
    std::unique_ptr<Curve> leader;
    std::shared_ptr<Surface> surface0;
    std::shared_ptr<Surface> surface1;

    IntersectionCurve() = default;

    IntersectionCurve(
        std::unique_ptr<Curve> leader_curve,
        std::shared_ptr<Surface> s0,
        std::shared_ptr<Surface> s1
    ) : leader(std::move(leader_curve))
      , surface0(std::move(s0))
      , surface1(std::move(s1)) {}

    // --- 特殊成员函数 (需在 Curve/Surface 定义后实现) ---

    IntersectionCurve(const IntersectionCurve& other);
    IntersectionCurve(IntersectionCurve&&) noexcept = default;
    IntersectionCurve& operator=(const IntersectionCurve& other);
    IntersectionCurve& operator=(IntersectionCurve&&) noexcept = default;
    ~IntersectionCurve() = default;
};

// ============================================================
// Curve — 4 个曲线变体
// ============================================================

class Curve {
public:
    using Variant = std::variant<
        geometry::Line<geometry::Point3>,
        geometry::BSplineCurve<geometry::Point3>,
        geometry::NurbsCurve,
        IntersectionCurve
    >;

    Curve() = default;

    // 从各变体隐式构造
    Curve(geometry::Line<geometry::Point3> line)           : data_(std::move(line)) {}
    Curve(geometry::BSplineCurve<geometry::Point3> bspline): data_(std::move(bspline)) {}
    Curve(geometry::NurbsCurve nurbs)                       : data_(std::move(nurbs)) {}
    Curve(IntersectionCurve ic)                             : data_(std::move(ic)) {}

    // --- 访问底层 variant ---

    const Variant& variant() const { return data_; }
    Variant& variant() { return data_; }

    // --- 求值接口 (委托给 CurveOps) ---

    geometry::Point3 subs(double t) const;
    geometry::Vector3 der(double t) const;
    geometry::Vector3 der2(double t) const;
    geometry::Vector3 derN(size_t n, double t) const;
    geometry::CurveDers<geometry::Vector3> ders(size_t n, double t) const;
    geometry::ParameterRange parameterRange() const;
    std::optional<double> period() const;
    std::pair<double, double> rangeTuple() const;
    geometry::Point3 front() const;
    geometry::Point3 back() const;
    std::pair<std::vector<double>, std::vector<geometry::Point3>>
        parameterDivision(std::pair<double, double> range, double tol) const;
    void transformBy(const geometry::Matrix4& mat);

    // --- 参数反求 ---

    std::optional<double> searchNearestParameter(const geometry::Point3& point, double hint, size_t trials = 100) const;
    std::optional<double> searchParameter(const geometry::Point3& point, double hint, size_t trials = 100) const;

    /// 将另一条曲线拼接至末尾
    Curve concat(const Curve& other) const;

    // --- 方向反转 (Invertible) ---

    void invert();
    Curve inverse() const;

    // --- 提升为 NURBS (4D 齐次坐标 B样条) ---

    /// 将任意曲线提升为 NURBS 表示 (BSplineCurve<Vector4>)
    /// 用于同伦曲面等需要统一曲线类型的操作
    geometry::BSplineCurve<geometry::Vector4> liftUp() const;

    // --- variant 索引 ---

    size_t index() const { return data_.index(); }

    template<typename T>
    bool holds() const { return std::holds_alternative<T>(data_); }

    template<typename T>
    const T& get() const { return std::get<T>(data_); }

    template<typename T>
    T& get() { return std::get<T>(data_); }

private:
    Variant data_;
};

// ============================================================
// Surface — 4 个曲面变体
// ============================================================

class Surface {
public:
    using Variant = std::variant<
        geometry::Plane,
        geometry::BSplineSurface<geometry::Point3>,
        geometry::NurbsSurface,
        geometry::Processor<geometry::RevolutedCurve<Curve>, geometry::Matrix4>,
        geometry::Processor<geometry::ExtrudedCurve<Curve>, geometry::Matrix4>
    >;

    Surface() = default;

    // 从各变体隐式构造
    Surface(geometry::Plane plane)                                                    : data_(std::move(plane)) {}
    Surface(geometry::BSplineSurface<geometry::Point3> bspline)                       : data_(std::move(bspline)) {}
    Surface(geometry::NurbsSurface nurbs)                                             : data_(std::move(nurbs)) {}
    Surface(geometry::Processor<geometry::RevolutedCurve<Curve>, geometry::Matrix4> p): data_(std::move(p)) {}
    Surface(geometry::Processor<geometry::ExtrudedCurve<Curve>, geometry::Matrix4> p)         : data_(std::move(p)) {}

    // --- 访问底层 variant ---

    const Variant& variant() const { return data_; }
    Variant& variant() { return data_; }

    // --- 求值接口 ---

    geometry::Point3 subs(double u, double v) const;
    geometry::Vector3 uder(double u, double v) const;
    geometry::Vector3 vder(double u, double v) const;
    geometry::Vector3 uuder(double u, double v) const;
    geometry::Vector3 uvder(double u, double v) const;
    geometry::Vector3 vvder(double u, double v) const;
    geometry::Vector3 derMN(size_t m, size_t n, double u, double v) const;
    geometry::Vector3 normal(double u, double v) const;
    std::pair<geometry::ParameterRange, geometry::ParameterRange> parameterRange() const;
    void transformBy(const geometry::Matrix4& mat);

    // --- 方向反转 ---

    void invert();
    Surface inverse() const;

    // --- variant 索引 ---

    size_t index() const { return data_.index(); }

    template<typename T>
    bool holds() const { return std::holds_alternative<T>(data_); }

    template<typename T>
    const T& get() const { return std::get<T>(data_); }

    template<typename T>
    T& get() { return std::get<T>(data_); }

private:
    Variant data_;
};

// ============================================================
// IntersectionCurve 特殊成员函数实现 (Curve/Surface 已完整)
// ============================================================

inline IntersectionCurve::IntersectionCurve(const IntersectionCurve& other)
    : leader(other.leader ? std::make_unique<Curve>(*other.leader) : nullptr)
    , surface0(other.surface0 ? std::make_shared<Surface>(*other.surface0) : nullptr)
    , surface1(other.surface1 ? std::make_shared<Surface>(*other.surface1) : nullptr) {}

inline IntersectionCurve& IntersectionCurve::operator=(const IntersectionCurve& other) {
    if (this != &other) {
        leader = other.leader ? std::make_unique<Curve>(*other.leader) : nullptr;
        surface0 = other.surface0 ? std::make_shared<Surface>(*other.surface0) : nullptr;
        surface1 = other.surface1 ? std::make_shared<Surface>(*other.surface1) : nullptr;
    }
    return *this;
}

} // namespace MulanGeo::BRep
