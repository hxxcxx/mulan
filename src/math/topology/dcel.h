/**
 * @file dcel.h
 * @brief DCEL（双向半边边表）— 2D 平面细分数据结构
 * @author hxxcxx
 * @date 2026-07-07
 *
 * 源自 BeyondConvex::dcel，适配至 mulan::math。
 *
 * 数据结构：三组记录 —— 顶点 / 半边 / 面。
 *   - 每条无向边由两条半边（twin，方向相反）表示。
 *   - 半边按面边界 CCW 方向链接（next/prev）。
 *   - 每个顶点指向一条从它出发的半边；每个面指向其外边界上的一条半边。
 *
 * 所有权：顶点/半边/面由 DCEL 持有（unique_ptr）；拓扑关系用裸指针。
 * 生命周期：DCEL 存活期间，其返回的指针有效；DCEL 析构后禁止使用。
 *
 * 已实现（构造 + 查询）：
 *   - createVertex/createEdge/createFace
 *   - connectHalfEdges/setFaceOfCycle
 *   - 顶点度数/边界判定、面边界遍历、validate
 *   - 构造器：多边形、带洞多边形、三角剖分、包围盒
 *
 * 未实现（原 BeyondConvex 即为 stub）：
 *   - splitFace / mergeFaces（对角线增删，需复杂拓扑维护）——保留接口返回空/原值。
 *   - buildVoronoiDiagram —— Voronoi 单元拼装由 voronoi 模块直接生成结果，不经此入口。
 *
 * 形态：类定义（含简单 getter/setter inline）＋ .cpp 实现。
 */
#pragma once

#include "../math_export.h"
#include "../linalg/vec2.h"
#include "../geom/point.h"

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mulan::math {

class HalfEdge;
class Face;

// ============================================================
// Vertex
// ============================================================

/// DCEL 顶点：平面上一点，指向一条从它出发的入射半边。
class Vertex {
public:
    Vertex() = default;
    explicit Vertex(Point2 coords, int id = -1) : coords_(coords), id_(id) {}

    const Point2& coords() const { return coords_; }
    Point2& coords() { return coords_; }

    int id() const { return id_; }
    void setId(int id) { id_ = id; }

    HalfEdge* incidentEdge() const { return incidentEdge_; }
    void setIncidentEdge(HalfEdge* e) { incidentEdge_ = e; }
    bool hasIncidentEdge() const { return incidentEdge_ != nullptr; }

    /// 度（入射边数）。沿 twin->next 绕顶点遍历计数。
    int degree() const;

    /// 是否位于细分边界（有任一入射半边落在无界面上）。
    bool isOnBoundary() const;

private:
    Point2 coords_;
    int id_ = -1;
    HalfEdge* incidentEdge_ = nullptr;  ///< 一条从本顶点出发的半边
};

// ============================================================
// HalfEdge
// ============================================================

/// DCEL 半边：有向边，携带 origin/twin/next/prev/face 五元拓扑。
class HalfEdge {
public:
    HalfEdge() = default;
    explicit HalfEdge(int id) : id_(id) {}

    int id() const { return id_; }
    void setId(int id) { id_ = id; }

    Vertex* origin() const { return origin_; }
    void setOrigin(Vertex* v) { origin_ = v; }

    /// 目标顶点（twin 的 origin）。
    Vertex* destination() const;

    HalfEdge* twin() const { return twin_; }
    void setTwin(HalfEdge* e) { twin_ = e; }

    HalfEdge* next() const { return next_; }
    void setNext(HalfEdge* e) { next_ = e; }

    HalfEdge* prev() const { return prev_; }
    void setPrev(HalfEdge* e) { prev_ = e; }

    Face* face() const { return face_; }
    void setFace(Face* f) { face_ = f; }

    bool hasTwin() const { return twin_ != nullptr; }
    bool hasNext() const { return next_ != nullptr; }
    bool hasPrev() const { return prev_ != nullptr; }
    bool hasFace() const { return face_ != nullptr; }

    /// 是否落在无界面（本半边无面 或 其 twin 无面）。
    bool isOnBoundary() const;

    /// 同面边界上的所有相邻半边（沿 next 遍历一圈）。
    std::vector<HalfEdge*> adjacentEdges() const;

private:
    int id_ = -1;
    Vertex* origin_ = nullptr;
    HalfEdge* twin_ = nullptr;
    HalfEdge* next_ = nullptr;
    HalfEdge* prev_ = nullptr;
    Face* face_ = nullptr;
};

// ============================================================
// Face
// ============================================================

/// DCEL 面：一个区域，有外边界（CCW）与若干内边界（洞，CW）。
class Face {
public:
    Face() = default;
    explicit Face(int id) : id_(id) {}

    int id() const { return id_; }
    void setId(int id) { id_ = id; }

    HalfEdge* outerComponent() const { return outerComponent_; }
    void setOuterComponent(HalfEdge* e) { outerComponent_ = e; }

    const std::vector<HalfEdge*>& innerComponents() const { return innerComponents_; }
    std::vector<HalfEdge*>& innerComponents() { return innerComponents_; }
    void addInnerComponent(HalfEdge* e) { innerComponents_.push_back(e); }

    bool hasOuterComponent() const { return outerComponent_ != nullptr; }
    bool hasHoles() const { return !innerComponents_.empty(); }

    /// 外边界顶点（CCW 顺序）。
    std::vector<Vertex*> outerBoundaryVertices() const;
    /// 外边界半边（CCW 顺序）。
    std::vector<HalfEdge*> outerBoundaryEdges() const;
    /// 第 holeIndex 个洞的顶点（CW 顺序）。
    std::vector<Vertex*> holeVertices(size_t holeIndex) const;
    /// 第 holeIndex 个洞的半边（CW 顺序）。
    std::vector<HalfEdge*> holeEdges(size_t holeIndex) const;
    /// 外边界边数。
    size_t outerBoundarySize() const;
    /// 全部边界边数（外 + 各洞）。
    size_t totalBoundarySize() const;
    /// 是否为无界面（无外边界）。
    bool isUnbounded() const { return outerComponent_ == nullptr; }

private:
    int id_ = -1;
    HalfEdge* outerComponent_ = nullptr;
    std::vector<HalfEdge*> innerComponents_;
};

// ============================================================
// DCEL
// ============================================================

/// DCEL 主结构：持有顶点/半边/面的所有权与构造/查询接口。
///
/// 不可拷贝（unique_ptr 所有权），可移动。
/// 返回的裸指针在 DCEL 存活期内有效。
class DCEL {
public:
    DCEL() = default;
    DCEL(const DCEL&) = delete;
    DCEL& operator=(const DCEL&) = delete;
    DCEL(DCEL&&) = default;
    DCEL& operator=(DCEL&&) = default;

    // ---------- 顶点 ----------
    Vertex* createVertex(const Point2& coords);
    const std::vector<std::unique_ptr<Vertex>>& vertices() const { return vertices_; }
    Vertex* vertex(int id) const {
        if (id < 0 || id >= static_cast<int>(vertices_.size()))
            return nullptr;
        return vertices_[id].get();
    }
    size_t vertexCount() const { return vertices_.size(); }

    // ---------- 半边 ----------
    /// 创建一条无向边（两条 twin 半边），返回从 origin 出发的半边。
    HalfEdge* createEdge(Vertex* origin, Vertex* twinOrigin);
    const std::vector<std::unique_ptr<HalfEdge>>& halfEdges() const { return halfEdges_; }
    HalfEdge* halfEdge(int id) const {
        if (id < 0 || id >= static_cast<int>(halfEdges_.size()))
            return nullptr;
        return halfEdges_[id].get();
    }
    size_t halfEdgeCount() const { return halfEdges_.size(); }
    size_t edgeCount() const { return halfEdges_.size() / 2; }  // 无向边数

    // ---------- 面 ----------
    Face* createFace();
    const std::vector<std::unique_ptr<Face>>& faces() const { return faces_; }
    Face* face(int id) const {
        if (id < 0 || id >= static_cast<int>(faces_.size()))
            return nullptr;
        return faces_[id].get();
    }
    size_t faceCount() const { return faces_.size(); }

    // ---------- 拓扑 ----------
    /// 链接两条半边：prev->next。
    void connectHalfEdges(HalfEdge* prev, HalfEdge* next) {
        if (!prev || !next)
            throw std::invalid_argument("DCEL::connectHalfEdges: null");
        prev->setNext(next);
        next->setPrev(prev);
    }
    /// 将 face 赋给从 start 开始的一圈半边。
    void setFaceOfCycle(HalfEdge* start, Face* face) {
        if (!start)
            return;
        HalfEdge* cur = start;
        do {
            cur->setFace(face);
            cur = cur->next();
        } while (cur && cur != start);
    }

    /// 对角线分裂面（未实现，返回 nullptr）。
    /// 原 BeyondConvex 即为占位实现；如需该能力需另写复杂拓扑维护。
    HalfEdge* splitFace(Face* /*face*/, Vertex* /*v1*/, Vertex* /*v2*/) { return nullptr; }
    /// 合并相邻面（未实现，返回 nullptr）。
    Face* mergeFaces(HalfEdge* /*edge*/) { return nullptr; }

    // ---------- 校验 ----------
    /// 校验结构完整性：twin 对称、面边界闭合。失败信息输出到 stderr。
    bool validate() const;

    void clear() {
        vertices_.clear();
        halfEdges_.clear();
        faces_.clear();
        nextVertexId_ = nextEdgeId_ = nextFaceId_ = 0;
    }

private:
    std::vector<std::unique_ptr<Vertex>> vertices_;
    std::vector<std::unique_ptr<HalfEdge>> halfEdges_;
    std::vector<std::unique_ptr<Face>> faces_;
    int nextVertexId_ = 0;
    int nextEdgeId_ = 0;
    int nextFaceId_ = 0;
};

// ============================================================
// 构造器（自由函数，构建常见 DCEL 配置）
// ============================================================

namespace dcel_builder {

/// 由 CCW 多边形顶点构造 DCEL，返回代表多边形内部的面。
/// 顶点数 < 3 抛 std::invalid_argument。
MATH_API Face* buildPolygon(const std::vector<Point2>& verts, DCEL* dcel);

/// 由外边界（CCW）+ 多个洞（CW）构造 DCEL。
MATH_API Face* buildPolygonWithHoles(const std::vector<Point2>& outer, const std::vector<std::vector<Point2>>& holes,
                                     DCEL* dcel);

/// 由顶点表 + 三角形下标构造三角剖分 DCEL，返回每个三角形对应的面。
/// 内部边（两三角形共享）自动配对 twin。
MATH_API std::vector<Face*> buildTriangulation(const std::vector<Point2>& verts,
                                               const std::vector<std::array<int, 3>>& tris, DCEL* dcel);

/// 矩形包围盒 DCEL。
MATH_API Face* buildBoundingBox(double minX, double minY, double maxX, double maxY, DCEL* dcel);

}  // namespace dcel_builder

}  // namespace mulan::math
