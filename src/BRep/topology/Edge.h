/**
 * @file Edge.h
 * @brief 边
 *
 * 方向系统：front/back 受 orientation 影响，absolute_front/back 不受影响。
 * `oriented_curve()` 根据 orientation 返回正向或反向曲线。
 * `inverse()` O(1) — 仅切换 orientation 标志位，不创建新曲线副本。
 *
 * 基于 truck-topology::edge::Edge。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "ID.h"
#include "Vertex.h"
#include "Errors.h"
#include <memory>
#include <functional>
#include <type_traits>
#include <stdexcept>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

namespace MulanGeo::BRep {

/// 边
/// @tparam P 点类型
/// @tparam C 曲线几何类型
template<typename P, typename C>
class Edge {
public:
    Edge() = default;

    // --- 构造 ---

    /// 构造并检查有效性（两端点不同）
    static Core::Result<Edge> tryNew(
        const Vertex<P>& front, const Vertex<P>& back, C curve
    ) {
        if (front.isSamePoint(back)) {
            return Core::Err<Edge>(makeError(TopologyError::SameVertex));
        }
        return Edge(front, back, std::move(curve));
    }

    /// 构造（不检查，失败抛异常）
    static Edge newChecked(const Vertex<P>& front, const Vertex<P>& back, C curve) {
        auto result = tryNew(front, back, std::move(curve));
        if (!result) throw std::runtime_error("Edge: same vertex");
        return std::move(*result);
    }

    /// 构造（不检查）
    static Edge newUnchecked(const Vertex<P>& front, const Vertex<P>& back, C curve) {
        return Edge(front, back, std::move(curve));
    }

    // --- 方向 ---

    bool orientation() const { return orientation_; }

    /// O(1) 反转：仅切换 orientation 标志
    void invert() { orientation_ = !orientation_; }

    /// 返回方向相反的新 Edge（共享同一 curve_）
    Edge inverse() const {
        Edge e = *this;
        e.invert();
        return e;
    }

    // --- 端点（受 orientation 影响） ---

    /// 正向起点（orientation=true 时为 front, false 时为 back）
    const Vertex<P>& front() const {
        return orientation_ ? vertices_.first : vertices_.second;
    }

    /// 正向终点
    const Vertex<P>& back() const {
        return orientation_ ? vertices_.second : vertices_.first;
    }

    // --- 绝对端点（不受 orientation 影响） ---

    const Vertex<P>& absoluteFront() const { return vertices_.first; }
    const Vertex<P>& absoluteBack() const { return vertices_.second; }

    /// 返回绝对端点的 pair（方便结构化绑定）
    std::pair<const Vertex<P>&, const Vertex<P>&> absoluteEnds() const {
        return {vertices_.first, vertices_.second};
    }

    // --- 曲线 ---

    /// 返回 curve 的副本
    C curve() const { return *curve_; }

    /// 根据 orientation 返回正向或反向曲线
    /// orientation=true → 返回 curve 的副本
    /// orientation=false → 返回 curve.inverse()（曲线需支持 Invertible）
    C orientedCurve() const {
        C c = *curve_;
        if (!orientation_) {
            // 假设 C 有 invert() 方法（来自 Geometry::Invertible）
            c.invert();
        }
        return c;
    }

    void setCurve(C c) { *curve_ = std::move(c); }

    // --- ID ---

    EdgeID<C> id() const { return EdgeID<C>(curve_); }

    /// 判断两条边是否引用同一份曲线数据（共享 curve_）
    bool isSame(const Edge& o) const { return id() == o.id(); }

    /// 引用计数
    size_t count() const { return curve_.use_count(); }

    // --- 克隆 ---

    /// 返回绝对方向 (orientation=true) 的独立克隆
    /// 共享底层 Vertex 和 curve_（引用计数增加）
    Edge absoluteClone() const {
        return Edge(vertices_.first, vertices_.second, *curve_);
    }

    // --- 一致性检查 ---

    /// 检查几何与拓扑的一致性
    /// 要求: curve.front() == front().point() && curve.back() == back().point()
    bool isGeometricConsistent() const {
        if (!curve_ || pointers_null()) return false;
        constexpr double tol = 1e-8;
        double t0 = curve_->rangeTuple().first;
        double t1 = curve_->rangeTuple().second;
        // 使用 Invertible 接口计算有参曲线端点
        P p0, p1;
        if (orientation_) {
            p0 = curve_->subs(t0);
            p1 = curve_->subs(t1);
        } else {
            p0 = curve_->subs(t1);
            p1 = curve_->subs(t0);
        }
        return nearPoints(front().point(), p0, tol) &&
               nearPoints(back().point(), p1, tol);
    }

    // --- 类型映射 ---

    template<typename Q, typename D>
    std::optional<Edge<Q, D>> tryMapped(
        std::function<std::optional<Q>(const P&)> pm,
        std::function<std::optional<D>(const C&)> cm
    ) const {
        auto v0 = vertices_.first.template tryMapped<Q>(pm);
        auto v1 = vertices_.second.template tryMapped<Q>(pm);
        auto cc = cm(*curve_);
        if (!v0 || !v1 || !cc) return std::nullopt;
        return Edge<Q, D>(std::move(*v0), std::move(*v1), std::move(*cc), orientation_);
    }

private:
    std::pair<Vertex<P>, Vertex<P>> vertices_;
    bool orientation_ = true;
    std::shared_ptr<C> curve_;

    Edge(Vertex<P> front, Vertex<P> back, C curve)
        : vertices_(std::move(front), std::move(back))
        , orientation_(true)
        , curve_(std::make_shared<C>(std::move(curve))) {}

    Edge(Vertex<P> front, Vertex<P> back, C curve, bool orientation)
        : vertices_(std::move(front), std::move(back))
        , orientation_(orientation)
        , curve_(std::make_shared<C>(std::move(curve))) {}

    bool pointers_null() const {
        return !vertices_.first.operator bool() ||
               !vertices_.second.operator bool() || !curve_;
    }

    static bool nearPoints(const P& a, const P& b, double tol) {
        if constexpr (std::is_same_v<P, glm::dvec3>) {
            return glm::length2(a - b) < tol * tol;
        } else if constexpr (std::is_same_v<P, glm::dvec2>) {
            return glm::length2(a - b) < tol * tol;
        } else {
            return glm::length(a - b) < tol;
        }
    }
};

} // namespace MulanGeo::BRep
