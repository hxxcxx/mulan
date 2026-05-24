/**
 * @file Wire.h
 * @brief 线框（有序边列表）
 *
 * 使用 std::deque 存储边，支持双向操作。
 *
 * 基于 truck-topology::wire::Wire。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "Edge.h"
#include "Errors.h"
#include <deque>
#include <optional>
#include <unordered_set>
#include <stdexcept>

namespace mulan::brep {

/// 线框
/// @tparam P 点类型
/// @tparam C 曲线几何类型
template<typename P, typename C>
class Wire {
public:
    using EdgeType = Edge<P, C>;

    Wire() = default;

    // --- 构造 ---

    /// 从边列表构造并检查有效性
    static core::Result<Wire> tryNew(std::deque<Edge<P, C>> edges) {
        if (edges.empty()) {
            return core::Err<Wire>(makeError(TopologyError::EmptyWire));
        }
        Wire w;
        w.edge_list_ = std::move(edges);
        if (!w.isContinuous()) {
            return core::Err<Wire>(makeError(TopologyError::NotConnected));
        }
        if (!w.isClosed()) {
            return core::Err<Wire>(makeError(TopologyError::NotClosedWire));
        }
        if (!w.isSimple()) {
            return core::Err<Wire>(makeError(TopologyError::NotSimpleWire));
        }
        return w;
    }

    /// 构造（不检查）
    static Wire newUnchecked(std::deque<Edge<P, C>> edges) {
        Wire w;
        w.edge_list_ = std::move(edges);
        return w;
    }

    // --- 访问 ---

    size_t len() const { return edge_list_.size(); }
    bool isEmpty() const { return edge_list_.empty(); }

    const Edge<P, C>& operator[](size_t i) const { return edge_list_[i]; }

    const std::deque<Edge<P, C>>& edges() const { return edge_list_; }

    // --- 修改 ---

    void pushBack(const Edge<P, C>& e) { edge_list_.push_back(e); }
    void pushFront(const Edge<P, C>& e) { edge_list_.push_front(e); }
    void popBack() { edge_list_.pop_back(); }
    void popFront() { edge_list_.pop_front(); }

    // --- 查询 ---

    /// 首尾顶点是否相同（闭合线框）
    bool isClosed() const {
        if (edge_list_.empty()) return false;
        return edge_list_.front().front().isSamePoint(edge_list_.back().back());
    }

    /// 是否简单线框（无自交顶点）
    bool isSimple() const {
        if (edge_list_.empty()) return true;
        std::unordered_set<VertexID<P>, typename VertexID<P>::Hash> seen;
        for (const auto& edge : edge_list_) {
            if (!seen.insert(edge.front().id()).second) return false;
        }
        // 闭合线框最后一个顶点就是第一个，允许重复
        if (isClosed()) return true;
        // 非闭合线框还需要检查最后一个顶点
        if (seen.find(edge_list_.back().back().id()) != seen.end()) return false;
        return true;
    }

    const Vertex<P>& frontVertex() const { return edge_list_.front().front(); }
    const Vertex<P>& backVertex() const { return edge_list_.back().back(); }

    // --- 方向 ---

    Wire inverse() const {
        Wire w;
        for (auto it = edge_list_.rbegin(); it != edge_list_.rend(); ++it) {
            w.edge_list_.push_back(it->inverse());
        }
        return w;
    }

    void invert() {
        *this = inverse();
    }

    // --- 判断 ---

    /// 每条边的 back 是否与下一条边的 front 连接
    bool isContinuous() const {
        for (size_t i = 0; i + 1 < edge_list_.size(); ++i) {
            if (!edge_list_[i].back().isSamePoint(edge_list_[i + 1].front()))
                return false;
        }
        return true;
    }

    /// 判断多个 Wire 是否不相交（用于 Face 的多边界检查）
    static bool disjointWires(const std::vector<Wire>& wires) {
        if (wires.size() <= 1) return true;
        std::unordered_set<VertexID<P>, typename VertexID<P>::Hash> all_vertices;
        for (const auto& wire : wires) {
            for (const auto& edge : wire.edge_list_) {
                if (all_vertices.find(edge.front().id()) != all_vertices.end()) {
                    return false;
                }
                all_vertices.insert(edge.front().id());
            }
            // 闭合线框的 back vertex = front vertex，不需要额外插入
        }
        return true;
    }

    // --- 类型映射 ---

    template<typename Q, typename D>
    std::optional<Wire<Q, D>> tryMapped(
        std::function<std::optional<Q>(const P&)> pm,
        std::function<std::optional<D>(const C&)> cm
    ) const {
        Wire<Q, D> result;
        for (const auto& edge : edge_list_) {
            auto mapped = edge.template tryMapped<Q, D>(pm, cm);
            if (!mapped) return std::nullopt;
            result.edge_list_.push_back(std::move(*mapped));
        }
        return result;
    }

private:
    std::deque<Edge<P, C>> edge_list_;
};

} // namespace mulan::BRep
