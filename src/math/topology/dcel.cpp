/**
 * @file dcel.cpp
 * @brief DCEL 实现（非内联成员与构造器）
 * @author hxxcxx
 * @date 2026-07-07
 */
#include "dcel.h"
#include "../math.h"

#include <mulan/core/log/log.h>

#include <map>
#include <stdexcept>

namespace mulan::math {

// ============================================================
// Vertex
// ============================================================

int Vertex::degree() const {
    if (!hasIncidentEdge())
        return 0;
    int deg = 0;
    HalfEdge* e = incidentEdge_;
    do {
        ++deg;
        if (e->hasTwin() && e->twin()->hasNext()) {
            e = e->twin()->next();
        } else {
            break;
        }
    } while (e && e != incidentEdge_);
    return deg;
}

bool Vertex::isOnBoundary() const {
    if (!hasIncidentEdge())
        return false;
    HalfEdge* e = incidentEdge_;
    do {
        if (e->isOnBoundary())
            return true;
        if (e->hasTwin() && e->twin()->hasNext()) {
            e = e->twin()->next();
        } else {
            break;
        }
    } while (e && e != incidentEdge_);
    return false;
}

// ============================================================
// HalfEdge
// ============================================================

Vertex* HalfEdge::destination() const {
    return twin_ ? twin_->origin() : nullptr;
}

bool HalfEdge::isOnBoundary() const {
    return face_ == nullptr || (twin_ && twin_->face() == nullptr);
}

std::vector<HalfEdge*> HalfEdge::adjacentEdges() const {
    std::vector<HalfEdge*> adj;
    if (!hasFace())
        return adj;
    HalfEdge* cur = const_cast<HalfEdge*>(this);
    do {
        adj.push_back(cur);
        cur = cur->next();
    } while (cur && cur != this);
    return adj;
}

// ============================================================
// Face
// ============================================================

std::vector<Vertex*> Face::outerBoundaryVertices() const {
    std::vector<Vertex*> vs;
    if (!hasOuterComponent())
        return vs;
    HalfEdge* start = outerComponent_;
    HalfEdge* cur = start;
    do {
        if (cur->origin())
            vs.push_back(cur->origin());
        cur = cur->next();
    } while (cur && cur != start);
    return vs;
}

std::vector<HalfEdge*> Face::outerBoundaryEdges() const {
    std::vector<HalfEdge*> es;
    if (!hasOuterComponent())
        return es;
    HalfEdge* start = outerComponent_;
    HalfEdge* cur = start;
    do {
        es.push_back(cur);
        cur = cur->next();
    } while (cur && cur != start);
    return es;
}

std::vector<Vertex*> Face::holeVertices(size_t holeIndex) const {
    std::vector<Vertex*> vs;
    if (holeIndex >= innerComponents_.size())
        return vs;
    HalfEdge* start = innerComponents_[holeIndex];
    HalfEdge* cur = start;
    do {
        if (cur->origin())
            vs.push_back(cur->origin());
        cur = cur->next();
    } while (cur && cur != start);
    return vs;
}

std::vector<HalfEdge*> Face::holeEdges(size_t holeIndex) const {
    std::vector<HalfEdge*> es;
    if (holeIndex >= innerComponents_.size())
        return es;
    HalfEdge* start = innerComponents_[holeIndex];
    HalfEdge* cur = start;
    do {
        es.push_back(cur);
        cur = cur->next();
    } while (cur && cur != start);
    return es;
}

size_t Face::outerBoundarySize() const {
    if (!hasOuterComponent())
        return 0;
    size_t n = 0;
    HalfEdge* start = outerComponent_;
    HalfEdge* cur = start;
    do {
        ++n;
        cur = cur->next();
    } while (cur && cur != start);
    return n;
}

size_t Face::totalBoundarySize() const {
    size_t total = outerBoundarySize();
    for (HalfEdge* start : innerComponents_) {
        HalfEdge* cur = start;
        do {
            ++total;
            cur = cur->next();
        } while (cur && cur != start);
    }
    return total;
}

// ============================================================
// DCEL
// ============================================================

Vertex* DCEL::createVertex(const Point2& coords) {
    auto v = std::make_unique<Vertex>(coords, nextVertexId_);
    Vertex* ptr = v.get();
    vertices_.push_back(std::move(v));
    ++nextVertexId_;
    return ptr;
}

HalfEdge* DCEL::createEdge(Vertex* origin, Vertex* twinOrigin) {
    if (!origin || !twinOrigin) {
        throw std::invalid_argument("DCEL::createEdge: null vertex");
    }
    auto e1 = std::make_unique<HalfEdge>(nextEdgeId_);
    auto e2 = std::make_unique<HalfEdge>(nextEdgeId_ + 1);
    HalfEdge* p1 = e1.get();
    HalfEdge* p2 = e2.get();
    p1->setOrigin(origin);
    p2->setOrigin(twinOrigin);
    p1->setTwin(p2);
    p2->setTwin(p1);
    halfEdges_.push_back(std::move(e1));
    halfEdges_.push_back(std::move(e2));
    nextEdgeId_ += 2;
    if (!origin->hasIncidentEdge())
        origin->setIncidentEdge(p1);
    if (!twinOrigin->hasIncidentEdge())
        twinOrigin->setIncidentEdge(p2);
    return p1;
}

Face* DCEL::createFace() {
    auto f = std::make_unique<Face>(nextFaceId_);
    Face* ptr = f.get();
    faces_.push_back(std::move(f));
    ++nextFaceId_;
    return ptr;
}

bool DCEL::validate() const {
    for (const auto& e : halfEdges_) {
        if (!e->hasTwin()) {
            LOG_ERROR("[DCEL] Validation failed: half-edge {} has no twin", e->id());
            return false;
        }
        if (e->twin()->twin() != e.get()) {
            LOG_ERROR("[DCEL] Validation failed: half-edge {} has a broken twin link", e->id());
            return false;
        }
    }
    for (const auto& f : faces_) {
        if (!f->hasOuterComponent())
            continue;
        HalfEdge* start = f->outerComponent();
        HalfEdge* cur = start;
        int count = 0;
        do {
            if (!cur) {
                LOG_ERROR("[DCEL] Validation failed: face {} has a broken cycle", f->id());
                return false;
            }
            if (cur->face() != f.get()) {
                LOG_ERROR("[DCEL] Validation failed: half-edge {} references the wrong face", cur->id());
                return false;
            }
            cur = cur->next();
            if (++count > 100000) {
                LOG_ERROR("[DCEL] Validation failed: face {} cycle exceeds the safety limit", f->id());
                return false;
            }
        } while (cur != start);
    }
    return true;
}

// ============================================================
// 构造器
// ============================================================

namespace dcel_builder {

Face* buildPolygon(const std::vector<Point2>& verts, DCEL* dcel) {
    if (verts.size() < 3)
        throw std::invalid_argument("buildPolygon: < 3 vertices");
    if (!dcel)
        throw std::invalid_argument("buildPolygon: null dcel");

    std::vector<Vertex*> vp;
    vp.reserve(verts.size());
    for (const Point2& p : verts)
        vp.push_back(dcel->createVertex(p));

    std::vector<HalfEdge*> boundary;
    boundary.reserve(vp.size());
    for (size_t i = 0; i < vp.size(); ++i) {
        boundary.push_back(dcel->createEdge(vp[i], vp[(i + 1) % vp.size()]));
    }
    for (size_t i = 0; i < boundary.size(); ++i) {
        dcel->connectHalfEdges(boundary[i], boundary[(i + 1) % boundary.size()]);
    }
    Face* f = dcel->createFace();
    f->setOuterComponent(boundary[0]);
    dcel->setFaceOfCycle(boundary[0], f);
    return f;
}

Face* buildPolygonWithHoles(const std::vector<Point2>& outer, const std::vector<std::vector<Point2>>& holes,
                            DCEL* dcel) {
    Face* f = buildPolygon(outer, dcel);
    for (const std::vector<Point2>& hole : holes) {
        if (hole.size() < 3)
            continue;
        std::vector<Vertex*> hp;
        hp.reserve(hole.size());
        for (const Point2& p : hole)
            hp.push_back(dcel->createVertex(p));
        std::vector<HalfEdge*> he;
        he.reserve(hp.size());
        for (size_t i = 0; i < hp.size(); ++i) {
            he.push_back(dcel->createEdge(hp[i], hp[(i + 1) % hp.size()]));
        }
        for (size_t i = 0; i < he.size(); ++i) {
            dcel->connectHalfEdges(he[i], he[(i + 1) % he.size()]);
        }
        f->addInnerComponent(he[0]);
        dcel->setFaceOfCycle(he[0], f);
    }
    return f;
}

std::vector<Face*> buildTriangulation(const std::vector<Point2>& verts, const std::vector<std::array<int, 3>>& tris,
                                      DCEL* dcel) {
    if (!dcel)
        throw std::invalid_argument("buildTriangulation: null dcel");
    std::vector<Vertex*> vp;
    vp.reserve(verts.size());
    for (const Point2& p : verts)
        vp.push_back(dcel->createVertex(p));

    // 有向边 -> 半边 的映射，便于识别共享边并配对 twin。
    // key = (from, to)，存从 from->to 的半边。
    std::map<std::pair<int, int>, HalfEdge*> edgeMap;
    std::vector<Face*> faces;
    faces.reserve(tris.size());

    for (const std::array<int, 3>& t : tris) {
        int idx[3] = { t[0], t[1], t[2] };
        HalfEdge* ring[3] = { nullptr, nullptr, nullptr };
        for (int k = 0; k < 3; ++k) {
            int a = idx[k];
            int b = idx[(k + 1) % 3];
            auto key = std::make_pair(a, b);
            auto revKey = std::make_pair(b, a);
            auto it = edgeMap.find(key);
            auto revIt = edgeMap.find(revKey);
            if (it != edgeMap.end()) {
                // 已有同向半边：直接复用（退化的重复三角形）
                ring[k] = it->second;
            } else if (revIt != edgeMap.end()) {
                // 反向已存在：取其 twin 作为本三角形该边的半边
                ring[k] = revIt->second->twin();
                edgeMap[key] = ring[k];
            } else {
                // 新边：创建，并登记正反两个方向
                HalfEdge* he = dcel->createEdge(vp[a], vp[b]);
                edgeMap[key] = he;
                edgeMap[revKey] = he->twin();
                ring[k] = he;
            }
        }
        dcel->connectHalfEdges(ring[0], ring[1]);
        dcel->connectHalfEdges(ring[1], ring[2]);
        dcel->connectHalfEdges(ring[2], ring[0]);
        Face* f = dcel->createFace();
        f->setOuterComponent(ring[0]);
        dcel->setFaceOfCycle(ring[0], f);
        faces.push_back(f);
    }
    return faces;
}

Face* buildBoundingBox(double minX, double minY, double maxX, double maxY, DCEL* dcel) {
    return buildPolygon({ Point2(minX, minY), Point2(maxX, minY), Point2(maxX, maxY), Point2(minX, maxY) }, dcel);
}

}  // namespace dcel_builder

}  // namespace mulan::math
