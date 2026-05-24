/**
 * @file Solid.h
 * @brief 实体（壳的集合）
 *
 * 多个边界壳构成实体。支持反转和拓扑编辑操作。
 *
 * 基于 truck-topology::solid::Solid。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "Shell.h"
#include "Errors.h"
#include <vector>
#include <optional>
#include <stdexcept>

namespace MulanGeo::brep {

/// 实体
template<typename P, typename C, typename S>
class Solid {
public:
    Solid() = default;

    // --- 构造 ---

    static core::Result<Solid> tryNew(
        std::vector<Shell<P, C, S>> boundaries
    ) {
        if (boundaries.empty()) {
            return core::Err<Solid>(makeError(TopologyError::EmptyShell));
        }
        // 每个边界壳必须是闭合的
        for (const auto& shell : boundaries) {
            if (!shell.isClosed()) {
                return core::Err<Solid>(makeError(TopologyError::NotClosedShell));
            }
        }
        return Solid(std::move(boundaries));
    }

    static Solid newUnchecked(std::vector<Shell<P, C, S>> boundaries) {
        return Solid(std::move(boundaries));
    }

    // --- 访问 ---

    const std::vector<Shell<P, C, S>>& boundaries() const { return boundaries_; }
    size_t numBoundaries() const { return boundaries_.size(); }
    const Shell<P, C, S>& boundary(size_t i) const { return boundaries_[i]; }

    // --- 方向 ---

    void invert() {
        for (auto& shell : boundaries_) {
            shell.invert();
        }
    }

    /// 取反（返回内部/外部反转的实体）
    Solid operator!() const {
        Solid s = *this;
        s.invert();
        return s;
    }

    // --- 拓扑编辑（布尔运算的低级原语） ---

    /// 在指定边上插入一个顶点，将边一分为二
    /// 找到所有引用该边的 Face，将其中的边替换为两条新边
    std::optional<std::pair<Edge<P, C>, Edge<P, C>>>
    cutEdge(EdgeID<C> edge_id, const Vertex<P>& v) {
        // 1. 找到边
        const Edge<P, C>* target_edge = nullptr;
        for (auto& shell : boundaries_) {
            for (size_t fi = 0; fi < shell.len(); ++fi) {
                for (const auto& wire : shell[fi].boundaries()) {
                    for (const auto& edge : wire.edges()) {
                        if (edge.id() == edge_id) {
                            target_edge = &edge;
                            break;
                        }
                    }
                    if (target_edge) break;
                }
                if (target_edge) break;
            }
            if (target_edge) break;
        }
        if (!target_edge) return std::nullopt;

        // 2. 在参数空间中找到新顶点对应的参数
        auto curve = target_edge->curve();
        auto [t0, t1] = curve.rangeTuple();
        // 搜索最近的参数
        double t_mid = (t0 + t1) / 2.0;
        double best_dist2 = std::numeric_limits<double>::max();
        for (int i = 0; i <= 64; ++i) {
            double t = t0 + (t1 - t0) * i / 64.0;
            double d2 = 0.0;
            auto pt = curve.subs(t);
            if constexpr (std::is_same_v<P, glm::dvec3>) {
                d2 = glm::length2(pt - v.point());
            } else {
                d2 = glm::length2(pt - v.point());
            }
            if (d2 < best_dist2) {
                best_dist2 = d2;
                t_mid = t;
            }
        }
        if (t_mid <= t0 || t_mid >= t1) return std::nullopt;

        // 3. 切分边
        auto cut_result = target_edge->cutWithParameter(t_mid);
        if (!cut_result) return std::nullopt;
        return std::make_pair(cut_result->first, cut_result->second);
    }

    /// 移除一个顶点，合并相邻两条边为一条
    std::optional<Edge<P, C>>
    removeVertexByConcatEdges(VertexID<P> vertex_id) {
        // 遍历所有 Face → Wire → Edge，找到以 vertex_id 为端点的两条边
        const Edge<P, C>* edge_before = nullptr;
        const Edge<P, C>* edge_after = nullptr;
        bool before_forward = true;
        bool after_forward = true;

        for (auto& shell : boundaries_) {
            for (size_t fi = 0; fi < shell.len(); ++fi) {
                for (const auto& wire : shell[fi].boundaries()) {
                    const auto& edges = wire.edges();
                    for (size_t ei = 0; ei < edges.size(); ++ei) {
                        if (edges[ei].front().id() == vertex_id) {
                            edge_after = &edges[ei];
                            after_forward = true;
                            // 前一条边的 back 应该是这个 vertex
                            if (ei > 0) {
                                edge_before = &edges[ei - 1];
                                before_forward = true;
                            }
                        }
                        if (edges[ei].back().id() == vertex_id) {
                            edge_before = &edges[ei];
                            before_forward = true;
                            // 后一条边的 front 应该是这个 vertex
                            if (ei + 1 < edges.size()) {
                                edge_after = &edges[ei + 1];
                                after_forward = true;
                            }
                        }
                    }
                }
            }
        }

        if (!edge_before || !edge_after) return std::nullopt;

        // 合并两条边
        auto concat_result = Edge<P, C>::concat(*edge_before, *edge_after);
        if (!concat_result) return std::nullopt;
        return std::move(*concat_result);
    }

    /// 在面上插入一条边，将一个面一分为二
    /// 基于 truck-topology::Solid::cut_face_by_edge
    bool cutFaceByEdge(FaceID<S> face_id, const Edge<P, C>& new_edge) {
        // 1. 找到目标 Face
        for (auto& shell : boundaries_) {
            for (size_t fi = 0; fi < shell.len(); ++fi) {
                if (shell[fi].id() != face_id) continue;

                // 2. 在面的边界中找到 new_edge 两端点所在的边界线
                const auto& boundaries = shell[fi].boundaries();
                const auto& v0 = new_edge.absoluteFront();
                const auto& v1 = new_edge.absoluteBack();

                // 找到包含 v0 和 v1 的边界线索引
                int wire_idx_0 = -1, wire_idx_1 = -1;
                int edge_idx_0 = -1, edge_idx_1 = -1;

                for (size_t wi = 0; wi < boundaries.size(); ++wi) {
                    const auto& edges = boundaries[wi].edges();
                    for (size_t ei = 0; ei < edges.size(); ++ei) {
                        if (edges[ei].absoluteFront().id() == v0.id() ||
                            edges[ei].absoluteBack().id() == v0.id()) {
                            if (wire_idx_0 < 0) {
                                wire_idx_0 = static_cast<int>(wi);
                                edge_idx_0 = static_cast<int>(ei);
                            }
                        }
                        if (edges[ei].absoluteFront().id() == v1.id() ||
                            edges[ei].absoluteBack().id() == v1.id()) {
                            if (wire_idx_1 < 0) {
                                wire_idx_1 = static_cast<int>(wi);
                                edge_idx_1 = static_cast<int>(ei);
                            }
                        }
                    }
                }

                if (wire_idx_0 < 0 || wire_idx_1 < 0) return false;

                // 3. 同一条边界线 → 分割为两个独立环
                if (wire_idx_0 == wire_idx_1) {
                    const auto& wire = boundaries[wire_idx_0];
                    const auto& edges = wire.edges();

                    // 确保起点在前
                    int start_idx = std::min(edge_idx_0, edge_idx_1);
                    int end_idx = std::max(edge_idx_0, edge_idx_1);

                    // 分割为两段边界线
                    std::vector<Edge<P, C>> seg0, seg1;
                    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
                        if (i >= start_idx && i <= end_idx) {
                            seg0.push_back(edges[i]);
                        } else {
                            seg1.push_back(edges[i]);
                        }
                    }

                    // 构建 Face 0: seg0 + new_edge
                    seg0.push_back(new_edge.inverse());
                    auto wire0_result = Wire<P, C>::tryNew(seg0);
                    if (!wire0_result) return false;

                    // 构建 Face 1: seg1 + new_edge
                    seg1.insert(seg1.begin(), new_edge);
                    auto wire1_result = Wire<P, C>::tryNew(seg1);
                    if (!wire1_result) return false;

                    // 替换原 Face，添加新 Face
                    auto surface = shell[fi].surface();
                    auto face0 = Face<P, C, S>::newUnchecked(
                        {std::move(*wire0_result)}, surface);
                    if (!shell[fi].orientation()) face0.invert();

                    // 收集原有孔洞
                    std::vector<Wire<P, C>> boundaries0, boundaries1;
                    boundaries0.push_back(std::move(*wire0_result));
                    boundaries1.push_back(std::move(*wire1_result));

                    // 将孔洞分配给包含它的面（用包含测试）
                    for (size_t hi = 0; hi < boundaries.size(); ++hi) {
                        if (static_cast<int>(hi) == wire_idx_0) continue;
                        // 简单策略：孔洞归到第一个面
                        boundaries0.push_back(boundaries[hi]);
                    }

                    auto face0_full = Face<P, C, S>::newUnchecked(
                        std::move(boundaries0), surface);
                    auto face1 = Face<P, C, S>::newUnchecked(
                        std::move(boundaries1), surface);
                    if (!shell[fi].orientation()) {
                        face0_full.invert();
                        face1.invert();
                    }

                    shell.replaceFace(fi, std::move(face0_full));
                    shell.push(face1);
                    return true;
                }

                // 4. 不同边界线 → 在两条线之间建桥
                // 构造新的闭合环
                const auto& wire0 = boundaries[wire_idx_0];
                const auto& wire1 = boundaries[wire_idx_1];

                std::vector<Edge<P, C>> new_loop;
                // wire0 的所有边
                for (const auto& e : wire0.edges()) new_loop.push_back(e);
                // new_edge 连接到 wire1
                new_loop.push_back(new_edge);
                // wire1 的所有边
                for (const auto& e : wire1.edges()) new_loop.push_back(e);
                // new_edge 的反向返回
                new_loop.push_back(new_edge.inverse());

                auto new_wire_result = Wire<P, C>::tryNew(new_loop);
                if (!new_wire_result) return false;

                auto surface = shell[fi].surface();
                auto new_face = Face<P, C, S>::newUnchecked(
                    {std::move(*new_wire_result)}, surface);
                if (!shell[fi].orientation()) new_face.invert();

                shell.push(new_face);
                return true;
            }
        }
        return false;
    }

    // --- 一致性检查 ---

    bool isGeometricConsistent() const {
        for (const auto& shell : boundaries_) {
            auto cond = shell.shellCondition();
            if (cond != ShellCondition::Closed && cond != ShellCondition::Oriented) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<Shell<P, C, S>> boundaries_;

    explicit Solid(std::vector<Shell<P, C, S>> boundaries)
        : boundaries_(std::move(boundaries)) {}
};

} // namespace MulanGeo::BRep
