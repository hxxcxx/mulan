/**
 * @file Processor.h
 * @brief 几何变换包装器
 *
 * 基于 truck-geometry::decorators::Processor。
 * 对任意几何对象施加 4x4 变换。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Export.h"
#include <memory>

namespace MulanGeo::Geometry {

/// 对几何对象施加变换的包装器
/// @tparam Geom 被包装的几何类型
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

    // --- 委托到内部几何 ---

    auto subs(auto... args) const {
        auto p = geometry_.subs(args...);
        return applyTransform(p);
    }

    // --- 变换应用 ---

    Point3 applyTransform(const Point3& p) const {
        auto v = transform_ * glm::dvec4(p, 1.0);
        return Point3(v);
    }

    Vector3 applyTransform(const Vector3& v) const {
        auto r = transform_ * glm::dvec4(v, 0.0);
        return Vector3(r);
    }

    Vector4 applyTransform(const Vector4& v) const {
        return transform_ * v;
    }

    // --- 变换 ---

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
        // 变换的逆
        transform_ = glm::inverse(transform_);
        if constexpr (requires { geometry_.invert(); }) {
            geometry_.invert();
        }
    }

private:
    Geom geometry_;
    Trans transform_;
};

} // namespace MulanGeo::Geometry
