/**
 * @file Vertex.h
 * @brief 顶点
 *
 * 持有 shared_ptr<P>，通过指针地址提供唯一身份。
 * C++ 不需要 Mutex — shared_ptr 允许通过非 const 访问修改内容。
 *
 * 基于 truck-topology::vertex::Vertex。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "ID.h"
#include <memory>
#include <vector>
#include <optional>
#include <functional>

namespace MulanGeo::BRep::topology {

/// 顶点
/// @tparam P 点类型 (Point3, Point2 等)
template<typename P>
class Vertex {
public:
    Vertex() = default;

    explicit Vertex(P point)
        : point_(std::make_shared<P>(std::move(point))) {}

    // --- 批量构造 ---

    static std::vector<Vertex> news(const std::vector<P>& points) {
        std::vector<Vertex> result;
        result.reserve(points.size());
        for (const auto& p : points) {
            result.emplace_back(p);
        }
        return result;
    }

    // --- 访问 ---

    const P& point() const { return *point_; }
    void setPoint(P p) { *point_ = std::move(p); }

    // --- ID ---

    VertexID<P> id() const { return VertexID<P>(point_); }
    size_t count() const { return point_.use_count(); }

    // --- 比较（基于指针地址，非值比较） ---

    bool operator==(const Vertex& o) const { return point_.get() == o.point_.get(); }
    bool operator!=(const Vertex& o) const { return !(*this == o); }

    bool isSamePoint(const Vertex& o) const {
        return point_.get() == o.point_.get();
    }

    // --- 类型映射 ---

    /// 将点类型从 P 映射到 Q，返回新的 Vertex
    template<typename Q>
    std::optional<Vertex<Q>> tryMapped(
        std::function<std::optional<Q>(const P&)> f
    ) const {
        auto q = f(*point_);
        if (!q) return std::nullopt;
        return Vertex<Q>(std::move(*q));
    }

    template<typename Q>
    Vertex<Q> mapped(std::function<Q(const P&)> f) const {
        return Vertex<Q>(f(*point_));
    }

private:
    std::shared_ptr<P> point_;
};

} // namespace MulanGeo::BRep::topology
