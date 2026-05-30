/**
 * @file Compressed.h
 * @brief 拓扑压缩序列化结构
 *
 * vertices 在 Shell 级别（不在 Solid 级别）:
 *   每个 CompressedShell 包含独立的顶点列表。
 *
 * CompressDirector: 通过 HashMap<ID, size_t> 在压缩过程中去重。
 *
 * 基于 truck-topology::compress。
 *
 * @author hxxcxx
 * @date 2026-05-21
 */
#pragma once

#include "../BRepExport.h"
#include "ID.h"
#include "Vertex.h"
#include "Edge.h"
#include "Wire.h"
#include "Face.h"
#include "Shell.h"
#include "Solid.h"
#include <vector>
#include <unordered_map>

namespace mulan::brep {

// ============================================================
// 压缩序列化结构
// ============================================================

template<typename C>
struct CompressedEdge {
    std::pair<size_t, size_t> vertices; // 索引到 CompressedShell::vertices
    C curve;
};

struct CompressedEdgeIndex {
    size_t index;        // 索引到 CompressedShell::edges
    bool orientation;    // 该 Wire 中此 Edge 的方向
};

template<typename S>
struct CompressedFace {
    std::vector<std::vector<CompressedEdgeIndex>> boundaries; // [0]=外环, [1..]=孔
    bool orientation;
    S surface;
};

template<typename P, typename C, typename S>
struct CompressedShell {
    std::vector<P> vertices;
    std::vector<CompressedEdge<C>> edges;
    std::vector<CompressedFace<S>> faces;
};

template<typename P, typename C, typename S>
struct CompressedSolid {
    std::vector<CompressedShell<P, C, S>> boundaries;
};

// ============================================================
// CompressDirector — 去重索引构建器
// ============================================================

template<typename P, typename C, typename S>
class CompressDirector {
public:
    using VertexMap = std::unordered_map<VertexID<P>, size_t, typename VertexID<P>::Hash>;
    using EdgeMap   = std::unordered_map<EdgeID<C>, size_t, typename EdgeID<C>::Hash>;

    /// 压缩单个 Shell
    CompressedShell<P, C, S> compress(const Shell<P, C, S>& shell) {
        CompressedShell<P, C, S> result;

        for (const auto& face : shell.faces()) {
            CompressedFace<S> cface;
            cface.orientation = face.orientation();
            cface.surface = face.surface();

            for (const auto& wire : face.absoluteBoundaries()) {
                std::vector<CompressedEdgeIndex> cwire;
                for (const auto& edge : wire.edges()) {
                    CompressedEdgeIndex cei;
                    cei.orientation = edge.orientation();

                    // 检查 Edge 是否已添加
                    auto eit = edge_map_.find(edge.id());
                    if (eit == edge_map_.end()) {
                        // 新 Edge — 添加
                        size_t edge_idx = result.edges.size();
                        edge_map_[edge.id()] = edge_idx;

                        // Vertex 去重
                        size_t vi0 = addVertex(edge.absoluteFront(), result.vertices);
                        size_t vi1 = addVertex(edge.absoluteBack(), result.vertices);

                        CompressedEdge<C> ce;
                        ce.vertices = {vi0, vi1};
                        ce.curve = edge.curve();
                        result.edges.push_back(std::move(ce));
                        cei.index = edge_idx;
                    } else {
                        cei.index = eit->second;
                    }
                    cwire.push_back(cei);
                }
                cface.boundaries.push_back(std::move(cwire));
            }
            result.faces.push_back(std::move(cface));
        }

        return result;
    }

    /// 提取单个 Shell（从压缩结构重建）
    Shell<P, C, S> extract(const CompressedShell<P, C, S>& cs) {
        // 重建顶点
        std::vector<Vertex<P>> verts;
        verts.reserve(cs.vertices.size());
        for (const auto& p : cs.vertices) {
            verts.emplace_back(p);
        }

        // 重建边
        std::vector<Edge<P, C>> edges;
        edges.reserve(cs.edges.size());
        for (const auto& ce : cs.edges) {
            edges.push_back(Edge<P, C>::newUnchecked(
                verts[ce.vertices.first],
                verts[ce.vertices.second],
                ce.curve
            ));
        }

        // 重建面
        std::vector<Face<P, C, S>> faces;
        faces.reserve(cs.faces.size());
        for (const auto& cf : cs.faces) {
            std::vector<Wire<P, C>> wires;
            for (const auto& cw : cf.boundaries) {
                std::deque<Edge<P, C>> wire_edges;
                for (const auto& cei : cw) {
                    auto edge = edges[cei.index];
                    if (!cei.orientation) {
                        edge = edge.inverse();
                    }
                    wire_edges.push_back(std::move(edge));
                }
                wires.push_back(Wire<P, C>::newUnchecked(std::move(wire_edges)));
            }
            auto face = Face<P, C, S>::newUnchecked(
                std::move(wires), cf.surface
            );
            if (!cf.orientation) {
                face.invert();
            }
            faces.push_back(std::move(face));
        }

        Shell<P, C, S> shell;
        for (auto& f : faces) {
            shell.push(std::move(f));
        }
        return shell;
    }

    void clear() {
        vertex_map_.clear();
        edge_map_.clear();
    }

private:
    VertexMap vertex_map_;
    EdgeMap edge_map_;

    size_t addVertex(const Vertex<P>& v, std::vector<P>& verts) {
        auto it = vertex_map_.find(v.id());
        if (it != vertex_map_.end()) {
            return it->second;
        }
        size_t idx = verts.size();
        vertex_map_[v.id()] = idx;
        verts.push_back(v.point());
        return idx;
    }
};

} // namespace mulan::brep
