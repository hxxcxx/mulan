#pragma once

#include "../BRepExport.h"
#include "../Topology/Vertex.h"
#include "../Topology/Edge.h"
#include "../Topology/Wire.h"
#include "../Topology/Face.h"
#include "../Topology/Shell.h"
#include "../Topology/Solid.h"
#include "../CurveSurface/CurveSurface.h"
#include "../CurveSurface/CurveOps.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Specified/Circle.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>
#include <MulanGeo/Geometry/Nurbs/NurbsCurve.h>
#include <MulanGeo/Geometry/Nurbs/BSplineSurface.h>
#include <MulanGeo/Geometry/Nurbs/NurbsSurface.h>
#include <MulanGeo/Geometry/Decorators/Processor.h>
#include <MulanGeo/Geometry/Decorators/TrimmedCurve.h>
#include <MulanGeo/Geometry/Decorators/ExtrudedCurve.h>
#include <MulanGeo/Geometry/Decorators/RevolutedCurve.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <cmath>
#include <vector>
#include <functional>
#include <unordered_map>

namespace MulanGeo::BRep::builder {

using Point3  = Geometry::Point3;
using Vector3 = Geometry::Vector3;
using Matrix4 = Geometry::Matrix4;

constexpr double BREP_PI = 3.14159265358979323846;

// ============================================================
// Vertex creation
// ============================================================

inline Vertex<Point3> vertex(Point3 p) {
    return Vertex<Point3>(std::move(p));
}

inline std::vector<Vertex<Point3>> vertices(const std::vector<Point3>& points) {
    std::vector<Vertex<Point3>> result;
    result.reserve(points.size());
    for (const auto& p : points)
        result.emplace_back(p);
    return result;
}

// ============================================================
// Edge creation
// ============================================================

inline Edge<Point3, Curve> line(const Vertex<Point3>& v0, const Vertex<Point3>& v1) {
    return Edge<Point3, Curve>::newUnchecked(v0, v1,
        Curve(Geometry::Line<Point3>(v0.point(), v1.point())));
}

enum class ArcConstraint { Transit, Tangent };

struct ArcConstraintValue {
    ArcConstraint kind;
    Point3 transit{};
    Vector3 tangent{};

    static ArcConstraintValue fromTransit(Point3 p) {
        return {ArcConstraint::Transit, std::move(p), {}};
    }
    static ArcConstraintValue fromTangent(Vector3 t) {
        return {ArcConstraint::Tangent, {}, std::move(t)};
    }
};

namespace detail {

inline Point3 circumCenter(Point3 pt0, Point3 pt1, Point3 pt2) {
    Vector3 vec0 = pt1 - pt0;
    Vector3 vec1 = pt2 - pt0;
    double a2 = glm::dot(vec0, vec0);
    double ab = glm::dot(vec0, vec1);
    double b2 = glm::dot(vec1, vec1);
    double det = a2 * b2 - ab * ab;
    if (Geometry::soSmall(det))
        return (pt0 + pt1 + pt2) / 3.0;
    double u = (a2 * b2 - ab * b2) / (2.0 * det);
    double v = (a2 * b2 - ab * a2) / (2.0 * det);
    return pt0 + u * vec0 + v * vec1;
}

inline std::vector<Point3> sampleArc(Point3 pt0, Point3 origin, Vector3 axis, double angle, size_t numSamples) {
    origin = origin + glm::dot(pt0 - origin, axis) * axis;
    Vector3 diag = pt0 - origin;
    Vector3 y_axis = glm::cross(axis, diag);
    Matrix4 mat = glm::transpose(glm::dmat4(
        glm::dvec4(diag, 0.0),
        glm::dvec4(y_axis, 0.0),
        glm::dvec4(axis, 0.0),
        glm::dvec4(origin, 1.0)));

    std::vector<Point3> points;
    points.reserve(numSamples + 1);
    for (size_t i = 0; i <= numSamples; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(numSamples) * angle;
        double ct = std::cos(t), st = std::sin(t);
        Point3 p = origin + diag * ct + y_axis * st + axis * glm::dot(pt0 - origin, axis);
        auto v = mat * glm::dvec4(Point3(ct, st, 0.0), 1.0);
        points.push_back(Point3(v));
    }
    // Direct evaluation
    points.clear();
    for (size_t i = 0; i <= numSamples; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(numSamples) * angle;
        double ct = std::cos(t), st = std::sin(t);
        Point3 p = origin + (diag * ct + y_axis * st) + axis * glm::dot(pt0 - origin, axis);
        points.push_back(p);
    }
    return points;
}

inline Curve makeArcCurve(Point3 pt0, Point3 origin, Vector3 axis, double angle) {
    // Sample the arc and create a degree-1 BSpline approximation
    origin = origin + glm::dot(pt0 - origin, axis) * axis;
    Vector3 diag = pt0 - origin;
    Vector3 y_axis = glm::cross(axis, diag);

    size_t ns = 8;
    std::vector<Point3> cps;
    for (size_t i = 0; i <= ns; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(ns) * angle;
        double ct = std::cos(t), st = std::sin(t);
        cps.push_back(origin + diag * ct + y_axis * st);
    }

    // Build degree-1 BSpline (piecewise linear approximation)
    size_t n_cp = cps.size();
    size_t deg = 1;
    std::vector<double> knots;
    for (size_t i = 0; i <= deg; ++i) knots.push_back(0.0);
    for (size_t i = 1; i < n_cp - deg; ++i)
        knots.push_back(static_cast<double>(i) / static_cast<double>(n_cp - deg));
    for (size_t i = 0; i <= deg; ++i) knots.push_back(1.0);

    return Curve(Geometry::BSplineCurve<Point3>(Geometry::KnotVec(knots), std::move(cps)));
}

inline Curve makeLineCurve(const Point3& p0, const Point3& p1) {
    return Curve(Geometry::Line<Point3>(p0, p1));
}

inline Surface makePlaneSurface(const Point3& origin, const Vector3& u, const Vector3& v) {
    return Surface(Geometry::Plane(origin, u, v));
}

inline Surface makeExtrudedSurface(const Curve& curve, const Vector3& vec) {
    // For Line curves, use Plane; for others, use ExtrudedCurve
    if (curve.holds<Geometry::Line<Point3>>()) {
        auto& line = curve.get<Geometry::Line<Point3>>();
        Point3 p0 = line.frontPoint();
        Point3 p1 = line.backPoint();
        return Surface(Geometry::Plane(p0, p1 - p0, vec));
    }
    // Generic: use RevolutedCurve wrapper or ExtrudedCurve
    // Since Surface variant only has Plane/BSplineSurface/NurbsSurface/Processor<RevolutedCurve>,
    // we approximate via Plane for now
    auto [t0, t1] = curve_rangeTuple(curve);
    Point3 start = curve_subs(curve, t0);
    Vector3 dir = curve_der(curve, (t0 + t1) * 0.5);
    return Surface(Geometry::Plane(start, dir, vec));
}

inline Surface makeRevolutedSurface(const Curve& curve, const Point3& origin, const Vector3& axis) {
    auto rev = Geometry::RevolutedCurve<Curve>(curve, origin, axis);
    return Surface(Geometry::Processor<Geometry::RevolutedCurve<Curve>, Matrix4>(std::move(rev), Matrix4(1.0)));
}

using EdgeMap = std::unordered_map<VertexID<Point3>, Edge<Point3, Curve>, typename VertexID<Point3>::Hash>;

inline Edge<Point3, Curve> connectVertices(
    const Vertex<Point3>& v0,
    const Vertex<Point3>& v1,
    const std::function<Curve(const Point3&, const Point3&)>& connectPoints)
{
    Curve c = connectPoints(v0.point(), v1.point());
    return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
}

inline Face<Point3, Curve, Surface> connectEdges(
    const Edge<Point3, Curve>& edge0,
    const Edge<Point3, Curve>& edge1,
    const std::function<Curve(const Point3&, const Point3&)>& connectPoints,
    const std::function<Surface(const Curve&, const Curve&)>& connectCurves)
{
    auto edge2 = connectVertices(edge0.front(), edge1.front(), connectPoints);
    auto edge3 = connectVertices(edge0.back(), edge1.back(), connectPoints);

    Surface surface = connectCurves(edge0.orientedCurve(), edge1.orientedCurve());

    bool ori = edge0.orientation();
    std::deque<Edge<Point3, Curve>> wire_edges;
    if (ori) {
        wire_edges.push_back(edge0);
        wire_edges.push_back(std::move(edge3));
        wire_edges.push_back(edge1.inverse());
        wire_edges.push_back(std::move(edge2).inverse());
    } else {
        wire_edges.push_back(std::move(edge2));
        wire_edges.push_back(edge1);
        wire_edges.push_back(std::move(edge3).inverse());
        wire_edges.push_back(edge0.inverse());
    }
    auto wire = Wire<Point3, Curve>::newUnchecked(std::move(wire_edges));
    auto face = Face<Point3, Curve, Surface>::newUnchecked({std::move(wire)}, std::move(surface));
    if (!ori) face.invert();
    return face;
}

inline Face<Point3, Curve, Surface> subConnectWires(
    const Edge<Point3, Curve>& edge0,
    const Edge<Point3, Curve>& edge1,
    const std::function<Curve(const Point3&, const Point3&)>& connectPoints,
    const std::function<Surface(const Curve&, const Curve&)>& connectCurves,
    EdgeMap& vemap)
{
    auto getOrCreate = [&](const Vertex<Point3>& v0, const Vertex<Point3>& v1) -> Edge<Point3, Curve> {
        auto id = v0.id();
        auto it = vemap.find(id);
        if (it != vemap.end()) return it->second;
        auto e = connectVertices(v0, v1, connectPoints);
        vemap[id] = e;
        return e;
    };

    auto edge2 = getOrCreate(edge0.front(), edge1.front()).inverse();
    auto edge3 = getOrCreate(edge0.back(), edge1.back());

    Curve c0 = edge0.orientedCurve();
    Curve c1 = edge1.orientedCurve();
    Surface surface = connectCurves(c0, c1);

    bool ori = edge0.orientation();
    std::deque<Edge<Point3, Curve>> wire_edges;
    if (ori) {
        wire_edges.push_back(edge0);
        wire_edges.push_back(std::move(edge3));
        wire_edges.push_back(edge1.inverse());
        wire_edges.push_back(std::move(edge2));
    } else {
        wire_edges.push_back(std::move(edge2).inverse());
        wire_edges.push_back(edge1);
        wire_edges.push_back(std::move(edge3).inverse());
        wire_edges.push_back(edge0.inverse());
    }
    auto wire = Wire<Point3, Curve>::newUnchecked(std::move(wire_edges));
    auto face = Face<Point3, Curve, Surface>::newUnchecked({std::move(wire)}, std::move(surface));
    if (!ori) face.invert();
    return face;
}

inline Shell<Point3, Curve, Surface> connectWires(
    const Wire<Point3, Curve>& wire0,
    const Wire<Point3, Curve>& wire1,
    const std::function<Curve(const Point3&, const Point3&)>& connectPoints,
    const std::function<Surface(const Curve&, const Curve&)>& connectCurves)
{
    EdgeMap vemap;
    Shell<Point3, Curve, Surface> shell;
    const auto& edges0 = wire0.edges();
    const auto& edges1 = wire1.edges();
    size_t n = std::min(edges0.size(), edges1.size());
    for (size_t i = 0; i < n; ++i) {
        auto face = subConnectWires(edges0[i], edges1[i], connectPoints, connectCurves, vemap);
        shell.push(std::move(face));
    }
    return shell;
}

inline Shell<Point3, Curve, Surface> connectEdgeVectors(
    const std::vector<Edge<Point3, Curve>>& edges0,
    const std::vector<Edge<Point3, Curve>>& edges1,
    const std::function<Curve(const Point3&, const Point3&)>& connectPoints,
    const std::function<Surface(const Curve&, const Curve&)>& connectCurves)
{
    EdgeMap vemap;
    Shell<Point3, Curve, Surface> shell;
    size_t n = std::min(edges0.size(), edges1.size());
    for (size_t i = 0; i < n; ++i) {
        auto face = subConnectWires(edges0[i], edges1[i], connectPoints, connectCurves, vemap);
        shell.push(std::move(face));
    }
    return shell;
}

inline Surface makeHomotopySurface(const Curve& c0, const Curve& c1) {
    // Homotopy: surface(u,v) = (1-v)*c0(u) + v*c1(u)
    // Simplified: use Plane from the four corner points
    Point3 p00 = curve_subs(c0, curve_rangeTuple(c0).first);
    Point3 p01 = curve_subs(c0, curve_rangeTuple(c0).second);
    Point3 p10 = curve_subs(c1, curve_rangeTuple(c1).first);
    Point3 p11 = curve_subs(c1, curve_rangeTuple(c1).second);

    // If c0 and c1 are both lines, create a Plane
    if (c0.holds<Geometry::Line<Point3>>() && c1.holds<Geometry::Line<Point3>>()) {
        return Surface(Geometry::Plane(p00, p01 - p00, p10 - p00));
    }
    // Otherwise use ExtrudedCurve approach or BSplineSurface
    // Simplified: Plane approximation
    return Surface(Geometry::Plane(p00, p01 - p00, p10 - p00));
}

inline Vector3 takeOneAxisByNormal(Vector3 n) {
    Vector3 a = glm::abs(n);
    if (a.x > a.z || a.y > a.z)
        return glm::normalize(Vector3(-n.y, n.x, 0.0));
    return glm::normalize(Vector3(-n.z, 0.0, n.x));
}

inline std::optional<Geometry::Plane> attachPlane(const std::vector<std::vector<Point3>>& pts) {
    if (pts.empty()) return std::nullopt;

    Point3 center(0.0);
    size_t total = 0;
    for (const auto& wire_pts : pts)
        for (const auto& p : wire_pts) { center += p; ++total; }
    if (total == 0) return std::nullopt;
    center /= static_cast<double>(total);

    Vector3 normal(0.0);
    for (const auto& wire_pts : pts)
        for (size_t i = 0; i < wire_pts.size(); ++i) {
            const Point3& p0 = wire_pts[i];
            const Point3& p1 = wire_pts[(i + 1) % wire_pts.size()];
            normal += glm::cross(p0 - center, p1 - center);
        }

    if (Geometry::soSmall(glm::length(normal))) return std::nullopt;
    normal = glm::normalize(normal);

    Vector3 a = takeOneAxisByNormal(normal);
    glm::dmat3 rot(a, glm::cross(normal, a), normal);
    glm::dmat4 mat3x4(1.0);
    mat3x4[0] = glm::dvec4(rot[0], 0.0);
    mat3x4[1] = glm::dvec4(rot[1], 0.0);
    mat3x4[2] = glm::dvec4(rot[2], 0.0);
    mat3x4[3] = glm::dvec4(0.0, 0.0, 0.0, 1.0);
    Matrix4 mat = mat3x4;
    Matrix4 inv_mat = glm::inverse(mat);

    std::vector<std::vector<Point3>> tpts = pts;
    for (auto& wire_pts : tpts)
        for (auto& p : wire_pts)
            p = Point3(inv_mat * glm::dvec4(p, 1.0));

    Geometry::BoundingBox3D bbox;
    for (const auto& wire_pts : tpts)
        for (const auto& p : wire_pts)
            bbox.push(p);

    Vector3 diag = bbox.diagonal();
    if (!Geometry::soSmall(diag.z)) return std::nullopt;

    double signed_area = 0.0;
    for (const auto& wire_pts : tpts)
        for (size_t i = 0; i < wire_pts.size(); ++i) {
            const Point3& p0 = wire_pts[i];
            const Point3& p1 = wire_pts[(i + 1) % wire_pts.size()];
            signed_area += (p1.x + p0.x) * (p1.y - p0.y);
        }

    Point3 minPt, maxPt;
    if (signed_area >= 0.0) {
        maxPt = bbox.maxPt;
        minPt = bbox.minPt;
    } else {
        maxPt = Point3(bbox.minPt.x, bbox.maxPt.y, bbox.minPt.z);
        minPt = Point3(bbox.maxPt.x, bbox.minPt.y, bbox.minPt.z);
    }

    Geometry::Plane plane(minPt, Point3(maxPt.x, minPt.y, minPt.z), Point3(minPt.x, maxPt.y, minPt.z));
    plane.transformBy(mat);
    return plane;
}

} // namespace detail

// ============================================================
// Circle arc
// ============================================================

inline Edge<Point3, Curve> circleArc(
    const Vertex<Point3>& v0,
    const Vertex<Point3>& v1,
    ArcConstraintValue constraint)
{
    Point3 pt0 = v0.point();
    Point3 pt1 = v1.point();

    if (constraint.kind == ArcConstraint::Transit) {
        Point3 origin = detail::circumCenter(pt0, pt1, constraint.transit);
        Vector3 vec0 = pt0 - constraint.transit;
        Vector3 vec1 = pt1 - constraint.transit;
        Vector3 axis = glm::normalize(glm::cross(vec1, vec0));
        double angle = 2.0 * BREP_PI - glm::acos(glm::clamp(
            glm::dot(vec0, vec1) / (glm::length(vec0) * glm::length(vec1)), -1.0, 1.0));
        Curve c = detail::makeArcCurve(pt0, origin, axis, angle);
        return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
    } else {
        Vector3 tangent0 = glm::normalize(constraint.tangent);
        Vector3 chord = pt1 - pt0;
        Vector3 axis = glm::normalize(glm::cross(tangent0, chord));
        Vector3 to_origin = glm::cross(axis, tangent0);
        double radius = glm::length2(chord) / (2.0 * glm::dot(chord, to_origin));
        Point3 origin = pt0 + radius * to_origin;

        Vector3 vec0 = pt0 - origin;
        Vector3 vec1 = pt1 - origin;
        double angle = glm::atan(glm::dot(axis, glm::cross(vec0, vec1)), glm::dot(vec0, vec1));
        if (angle <= 0.0) angle += 2.0 * BREP_PI;

        Curve c = detail::makeArcCurve(pt0, origin, axis, angle);
        return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
    }
}

// ============================================================
// Bezier curve
// ============================================================

inline Edge<Point3, Curve> bezier(
    const Vertex<Point3>& v0,
    const Vertex<Point3>& v1,
    std::vector<Point3> inter_points)
{
    std::vector<Point3> ctrl_pts;
    ctrl_pts.push_back(v0.point());
    for (auto& p : inter_points) ctrl_pts.push_back(std::move(p));
    ctrl_pts.push_back(v1.point());

    size_t degree = ctrl_pts.size() - 1;
    Geometry::KnotVec kv = Geometry::KnotVec::bezier_knot(degree);
    auto curve = Geometry::BSplineCurve<Point3>(std::move(kv), std::move(ctrl_pts));
    return Edge<Point3, Curve>::newUnchecked(v0, v1, Curve(std::move(curve)));
}

// ============================================================
// Homotopy
// ============================================================

inline Face<Point3, Curve, Surface> homotopy(
    const Edge<Point3, Curve>& edge0,
    const Edge<Point3, Curve>& edge1)
{
    auto edge_right = line(edge0.front(), edge1.back());
    auto edge_left_inv = line(edge1.front(), edge0.back());

    std::deque<Edge<Point3, Curve>> wire_edges;
    wire_edges.push_back(edge0);
    wire_edges.push_back(std::move(edge_right));
    wire_edges.push_back(edge1.inverse());
    wire_edges.push_back(line(edge1.front(), edge0.back()).inverse());
    auto wire = Wire<Point3, Curve>::newUnchecked(std::move(wire_edges));

    Surface surface = detail::makeHomotopySurface(edge0.orientedCurve(), edge1.orientedCurve());
    return Face<Point3, Curve, Surface>::newUnchecked({std::move(wire)}, std::move(surface));
}

inline Core::Result<Shell<Point3, Curve, Surface>> tryWireHomotopy(
    const Wire<Point3, Curve>& wire0,
    const Wire<Point3, Curve>& wire1)
{
    if (wire0.len() != wire1.len()) {
        return Core::Err<Shell<Point3, Curve, Surface>>(
            Core::Error::make(static_cast<int32_t>(7), "wires have different number of edges"));
    }

    auto connectPoints = [](const Point3& p0, const Point3& p1) -> Curve {
        return detail::makeLineCurve(p0, p1);
    };
    auto connectCurves = [](const Curve& c0, const Curve& c1) -> Surface {
        return detail::makeHomotopySurface(c0, c1);
    };

    Shell<Point3, Curve, Surface> shell = detail::connectWires(wire0, wire1, connectPoints, connectCurves);
    return Core::Ok(std::move(shell));
}

// ============================================================
// Attach plane
// ============================================================

inline Core::Result<Face<Point3, Curve, Surface>> tryAttachPlane(
    const std::vector<Wire<Point3, Curve>>& wires)
{
    auto face_result = Face<Point3, Curve, Surface>::tryNew(
        std::vector<Wire<Point3, Curve>>(wires), Geometry::Plane());
    if (!face_result) return Core::Err<Face<Point3, Curve, Surface>>(face_result.error());

    std::vector<std::vector<Point3>> pts;
    for (const auto& wire : wires) {
        std::vector<Point3> wire_pts;
        for (const auto& edge : wire.edges()) {
            Curve c = edge.orientedCurve();
            auto [t0, t1] = curve_rangeTuple(c);
            wire_pts.push_back(curve_subs(c, t0));
            wire_pts.push_back(curve_subs(c, (t0 + t1) / 2.0));
        }
        pts.push_back(std::move(wire_pts));
    }

    auto plane_opt = detail::attachPlane(pts);
    if (!plane_opt)
        return Core::Err<Face<Point3, Curve, Surface>>(
            Core::Error::make(2, "wires are not in one plane"));

    return Core::Ok(Face<Point3, Curve, Surface>::newUnchecked(
        std::vector<Wire<Point3, Curve>>(wires), Surface(std::move(*plane_opt))));
}

// ============================================================
// Clone / Transform
// ============================================================

inline Vertex<Point3> clone(const Vertex<Point3>& v) { return Vertex<Point3>(v.point()); }

inline Edge<Point3, Curve> clone(const Edge<Point3, Curve>& e) { return e.absoluteClone(); }

inline Wire<Point3, Curve> clone(const Wire<Point3, Curve>& w) {
    std::deque<Edge<Point3, Curve>> edges;
    for (const auto& e : w.edges()) edges.push_back(clone(e));
    return Wire<Point3, Curve>::newUnchecked(std::move(edges));
}

inline Face<Point3, Curve, Surface> clone(const Face<Point3, Curve, Surface>& f) {
    std::vector<Wire<Point3, Curve>> bds;
    for (const auto& w : f.boundaries()) bds.push_back(clone(w));
    auto face = Face<Point3, Curve, Surface>::newUnchecked(std::move(bds), f.surface());
    if (!f.orientation()) face.invert();
    return face;
}

inline Vertex<Point3> transformed(const Vertex<Point3>& v, const Matrix4& mat) {
    return Vertex<Point3>(Point3(mat * glm::dvec4(v.point(), 1.0)));
}

inline Edge<Point3, Curve> transformed(const Edge<Point3, Curve>& e, const Matrix4& mat) {
    auto v0 = transformed(e.absoluteFront(), mat);
    auto v1 = transformed(e.absoluteBack(), mat);
    Curve c = e.curve();
    curve_transformBy(c, mat);
    return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
}

inline Wire<Point3, Curve> transformed(const Wire<Point3, Curve>& w, const Matrix4& mat) {
    std::deque<Edge<Point3, Curve>> edges;
    for (const auto& e : w.edges()) edges.push_back(transformed(e, mat));
    return Wire<Point3, Curve>::newUnchecked(std::move(edges));
}

inline Face<Point3, Curve, Surface> transformed(const Face<Point3, Curve, Surface>& f, const Matrix4& mat) {
    std::vector<Wire<Point3, Curve>> bds;
    for (const auto& w : f.boundaries()) bds.push_back(transformed(w, mat));
    Surface s = f.surface();
    surface_transformBy(s, mat);
    auto face = Face<Point3, Curve, Surface>::newUnchecked(std::move(bds), std::move(s));
    if (!f.orientation()) face.invert();
    return face;
}

inline Shell<Point3, Curve, Surface> transformed(const Shell<Point3, Curve, Surface>& sh, const Matrix4& mat) {
    Shell<Point3, Curve, Surface> result;
    for (size_t i = 0; i < sh.len(); ++i) result.push(transformed(sh[i], mat));
    return result;
}

inline Solid<Point3, Curve, Surface> transformed(const Solid<Point3, Curve, Surface>& solid, const Matrix4& mat) {
    std::vector<Shell<Point3, Curve, Surface>> bds;
    for (const auto& sh : solid.boundaries()) bds.push_back(transformed(sh, mat));
    return Solid<Point3, Curve, Surface>::newUnchecked(std::move(bds));
}

inline Vertex<Point3> translated(const Vertex<Point3>& v, Vector3 vec) {
    return transformed(v, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
}
inline Edge<Point3, Curve> translated(const Edge<Point3, Curve>& e, Vector3 vec) {
    return transformed(e, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
}
inline Wire<Point3, Curve> translated(const Wire<Point3, Curve>& w, Vector3 vec) {
    return transformed(w, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
}
inline Face<Point3, Curve, Surface> translated(const Face<Point3, Curve, Surface>& f, Vector3 vec) {
    return transformed(f, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
}
inline Shell<Point3, Curve, Surface> translated(const Shell<Point3, Curve, Surface>& sh, Vector3 vec) {
    return transformed(sh, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
}
inline Solid<Point3, Curve, Surface> translated(const Solid<Point3, Curve, Surface>& s, Vector3 vec) {
    return transformed(s, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
}

inline Matrix4 rotationMatrix(Point3 origin, Vector3 axis, double angle) {
    auto m0 = Matrix4(glm::translate(glm::dmat4(1.0), -origin));
    auto m1 = Matrix4(glm::rotate(glm::dmat4(1.0), angle, axis));
    auto m2 = Matrix4(glm::translate(glm::dmat4(1.0), origin));
    return m2 * m1 * m0;
}

inline Matrix4 scaleMatrix(Point3 origin, Vector3 scales) {
    auto m0 = Matrix4(glm::translate(glm::dmat4(1.0), -origin));
    auto m1 = Matrix4(glm::scale(glm::dmat4(1.0), scales));
    auto m2 = Matrix4(glm::translate(glm::dmat4(1.0), origin));
    return m2 * m1 * m0;
}

inline Vertex<Point3> rotated(const Vertex<Point3>& v, Point3 o, Vector3 a, double ang) { return transformed(v, rotationMatrix(o, a, ang)); }
inline Edge<Point3, Curve> rotated(const Edge<Point3, Curve>& e, Point3 o, Vector3 a, double ang) { return transformed(e, rotationMatrix(o, a, ang)); }
inline Wire<Point3, Curve> rotated(const Wire<Point3, Curve>& w, Point3 o, Vector3 a, double ang) { return transformed(w, rotationMatrix(o, a, ang)); }
inline Face<Point3, Curve, Surface> rotated(const Face<Point3, Curve, Surface>& f, Point3 o, Vector3 a, double ang) { return transformed(f, rotationMatrix(o, a, ang)); }
inline Shell<Point3, Curve, Surface> rotated(const Shell<Point3, Curve, Surface>& sh, Point3 o, Vector3 a, double ang) { return transformed(sh, rotationMatrix(o, a, ang)); }
inline Solid<Point3, Curve, Surface> rotated(const Solid<Point3, Curve, Surface>& s, Point3 o, Vector3 a, double ang) { return transformed(s, rotationMatrix(o, a, ang)); }

inline Vertex<Point3> scaled(const Vertex<Point3>& v, Point3 o, Vector3 s) { return transformed(v, scaleMatrix(o, s)); }
inline Edge<Point3, Curve> scaled(const Edge<Point3, Curve>& e, Point3 o, Vector3 s) { return transformed(e, scaleMatrix(o, s)); }
inline Wire<Point3, Curve> scaled(const Wire<Point3, Curve>& w, Point3 o, Vector3 s) { return transformed(w, scaleMatrix(o, s)); }
inline Face<Point3, Curve, Surface> scaled(const Face<Point3, Curve, Surface>& f, Point3 o, Vector3 s) { return transformed(f, scaleMatrix(o, s)); }

} // namespace MulanGeo::BRep::builder