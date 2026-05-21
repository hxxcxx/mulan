/**
 * @file Shell.h
 * @brief 壳（面的集合）
 *
 * 支持连通性检测、定向检测、流形检测。
 *
 * 基于 truck-topology::shell::Shell。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "Face.h"
#include "Edge.h"
#include "ID.h"
#include "Errors.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <stdexcept>

namespace MulanGeo::BRep::topology {

enum class ShellCondition : uint8_t {
    Open,    // 非闭合壳
    Closed,  // 闭合壳
    Oriented,// 闭合且定向一致的壳
};

/// 壳
template<typename P, typename C, typename S>
class Shell {
public:
    using FaceType = Face<P, C, S>;

    Shell() = default;

    // --- 访问 ---

    size_t len() const { return face_list_.size(); }
    bool isEmpty() const { return face_list_.empty(); }
    const Face<P, C, S>& operator[](size_t i) const { return face_list_[i]; }
    const std::vector<Face<P, C, S>>& faces() const { return face_list_; }

    void push(const Face<P, C, S>& f) { face_list_.push_back(f); }

    // --- 拓扑查询 ---

    /// 是否连通（通过 EdgeID 邻接图 BFS）
    bool isConnected() const {
        if (face_list_.empty()) return true;
        if (face_list_.size() == 1) return true;

        // 构建 EdgeID → Face 索引的邻接表
        std::unordered_map<EdgeID<C>, std::vector<size_t>, typename EdgeID<C>::Hash> edge_to_faces;
        for (size_t i = 0; i < face_list_.size(); ++i) {
            for (const auto& wire : face_list_[i].absoluteBoundaries()) {
                for (const auto& edge : wire.edges()) {
                    edge_to_faces[edge.id()].push_back(i);
                }
            }
        }

        // BFS
        std::queue<size_t> q;
        std::vector<bool> visited(face_list_.size(), false);
        q.push(0);
        visited[0] = true;
        size_t visited_count = 1;

        while (!q.empty()) {
            size_t fi = q.front(); q.pop();
            // 收集此 Face 的所有 EdgeID
            std::unordered_set<EdgeID<C>, typename EdgeID<C>::Hash> face_edges;
            for (const auto& wire : face_list_[fi].absoluteBoundaries()) {
                for (const auto& edge : wire.edges()) {
                    face_edges.insert(edge.id());
                }
            }
            // 通过 EdgeID 找到相邻 Face
            for (const auto& eid : face_edges) {
                auto it = edge_to_faces.find(eid);
                if (it == edge_to_faces.end()) continue;
                for (size_t nfi : it->second) {
                    if (nfi != fi && !visited[nfi]) {
                        visited[nfi] = true;
                        q.push(nfi);
                        ++visited_count;
                    }
                }
            }
        }

        return visited_count == face_list_.size();
    }

    /// 是否定向一致（每条边被两个相反方向的 Face 共享）
    bool isOriented() const {
        // 每条边必须被共享它的两个 Face 以相反方向引用
        std::unordered_map<EdgeID<C>, std::vector<std::pair<size_t, bool>>,
            typename EdgeID<C>::Hash> edge_orientation;

        for (size_t i = 0; i < face_list_.size(); ++i) {
            const auto& face = face_list_[i];
            for (const auto& wire : face.absoluteBoundaries()) {
                for (const auto& edge : wire.edges()) {
                    edge_orientation[edge.id()].push_back({i, edge.orientation()});
                }
            }
        }

        for (const auto& [eid, entries] : edge_orientation) {
            if (entries.size() == 1) continue; // 边界边
            if (entries.size() != 2) return false;
            // 两个方向必须相反
            if (entries[0].second == entries[1].second) return false;
        }
        return true;
    }

    /// 壳状态
    ShellCondition shellCondition() const {
        if (!isClosed()) return ShellCondition::Open;
        if (isOriented()) return ShellCondition::Oriented;
        return ShellCondition::Closed;
    }

    /// 是否闭合（所有边被恰好两个 Face 共享）
    bool isClosed() const {
        std::unordered_map<EdgeID<C>, size_t, typename EdgeID<C>::Hash> edge_count;
        for (const auto& face : face_list_) {
            for (const auto& wire : face.absoluteBoundaries()) {
                for (const auto& edge : wire.edges()) {
                    edge_count[edge.id()]++;
                }
            }
        }
        for (const auto& [eid, count] : edge_count) {
            if (count != 2) return false;
        }
        return true;
    }

    /// 边界线（仅被一个 Face 使用的边构成的线框）
    std::vector<Wire<P, C>> boundaries() const {
        std::unordered_map<EdgeID<C>, std::pair<size_t, const Edge<P, C>*>,
            typename EdgeID<C>::Hash> edge_map;
        for (const auto& face : face_list_) {
            for (const auto& wire : face.absoluteBoundaries()) {
                for (const auto& edge : wire.edges()) {
                    edge_map[edge.id()].first++;
                    edge_map[edge.id()].second = &edge;
                }
            }
        }

        // 收集边界边
        std::vector<const Edge<P, C>*> boundary_edges;
        for (const auto& [eid, info] : edge_map) {
            if (info.first == 1) {
                boundary_edges.push_back(info.second);
            }
        }

        // 简单排序连接成线框（后续可优化）
        std::vector<Wire<P, C>> wires;
        if (boundary_edges.empty()) return wires;
        // 简化实现：将所有边界边放入一个 Wire（实际需要排序连接）
        std::deque<Edge<P, C>> edge_deque;
        for (auto* e : boundary_edges) {
            edge_deque.push_back(*e);
        }
        if (!edge_deque.empty()) {
            wires.push_back(Wire<P, C>::newUnchecked(std::move(edge_deque)));
        }
        return wires;
    }

    void invert() {
        for (auto& face : face_list_) {
            face.invert();
        }
    }

    /// 相邻面信息
    struct AdjacentFace {
        const Face<P, C, S>* face;
        std::vector<EdgeID<C>> commonEdges;
    };

    std::vector<AdjacentFace> adjacentFaces(const Face<P, C, S>& face) const {
        // 收集此面的所有 EdgeID
        std::unordered_set<EdgeID<C>, typename EdgeID<C>::Hash> face_edges;
        for (const auto& wire : face.absoluteBoundaries()) {
            for (const auto& edge : wire.edges()) {
                face_edges.insert(edge.id());
            }
        }

        std::vector<AdjacentFace> result;
        for (const auto& f : face_list_) {
            if (f.isSame(face)) continue;
            std::vector<EdgeID<C>> common;
            for (const auto& wire : f.absoluteBoundaries()) {
                for (const auto& edge : wire.edges()) {
                    if (face_edges.count(edge.id())) {
                        common.push_back(edge.id());
                    }
                }
            }
            if (!common.empty()) {
                result.push_back({&f, std::move(common)});
            }
        }
        return result;
    }

private:
    std::vector<Face<P, C, S>> face_list_;
};

} // namespace MulanGeo::BRep::topology
