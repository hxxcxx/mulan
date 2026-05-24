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

#include <mulan/Geometry/Types.h>
#include <mulan/Geometry/Tolerance.h>
#include <mulan/Geometry/Specified/Line.h>
#include <mulan/Geometry/Specified/Plane.h>
#include <mulan/Geometry/Specified/Circle.h>
#include <mulan/Geometry/Nurbs/BSplineCurve.h>
#include <mulan/Geometry/Nurbs/NurbsCurve.h>
#include <mulan/Geometry/Nurbs/BSplineSurface.h>
#include <mulan/Geometry/Nurbs/NurbsSurface.h>
#include <mulan/Geometry/Decorators/Processor.h>
#include <mulan/Geometry/Decorators/TrimmedCurve.h>
#include <mulan/Geometry/Decorators/ExtrudedCurve.h>
#include <mulan/Geometry/Decorators/RevolutedCurve.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <cmath>
#include <vector>
#include <functional>
#include <unordered_map>

namespace mulan::brep {

class Builder {
public:
    using Point3  = geometry::Point3;
    using Vector3 = geometry::Vector3;
    using Vector4 = geometry::Vector4;
    using Matrix4 = geometry::Matrix4;

    static constexpr double BREP_PI = 3.14159265358979323846;

    Builder() = delete;

    // ============================================================
    // Vertex creation
    // ============================================================

    static inline Vertex<Point3> vertex(Point3 p) {
        return Vertex<Point3>(std::move(p));
    }

    static inline std::vector<Vertex<Point3>> vertices(const std::vector<Point3>& points) {
        std::vector<Vertex<Point3>> result;
        result.reserve(points.size());
        for (const auto& p : points)
            result.emplace_back(p);
        return result;
    }

    // ============================================================
    // Edge creation
    // ============================================================

    static inline Edge<Point3, Curve> line(const Vertex<Point3>& v0, const Vertex<Point3>& v1) {
        return Edge<Point3, Curve>::newUnchecked(v0, v1,
            Curve(geometry::Line<Point3>(v0.point(), v1.point())));
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

    struct Detail {
        static inline Point3 circumCenter(Point3 pt0, Point3 pt1, Point3 pt2) {
            Vector3 vec0 = pt1 - pt0;
            Vector3 vec1 = pt2 - pt0;
            double a2 = glm::dot(vec0, vec0);
            double ab = glm::dot(vec0, vec1);
            double b2 = glm::dot(vec1, vec1);
            double det = a2 * b2 - ab * ab;
            if (geometry::soSmall(det))
                return (pt0 + pt1 + pt2) / 3.0;
            double u = (a2 * b2 - ab * b2) / (2.0 * det);
            double v = (a2 * b2 - ab * a2) / (2.0 * det);
            return pt0 + u * vec0 + v * vec1;
        }

        static inline Curve makeArcCurve(Point3 pt0, Point3 origin, Vector3 axis, double angle) {
            // Project origin onto the plane containing pt0 and perpendicular to axis
            origin = origin + glm::dot(pt0 - origin, axis) * axis;
            Vector3 diag = pt0 - origin;
            Vector3 y_axis = glm::cross(axis, diag);

            // Determine the number of arc segments (each segment spans at most PI/2)
            size_t n_segs = static_cast<size_t>(std::ceil(std::abs(angle) / (BREP_PI / 2.0)));
            if (n_segs < 1) n_segs = 1;
            double seg_angle = angle / static_cast<double>(n_segs);

            // For a single segment (angle <= PI/2), use the standard 3-point quadratic NURBS arc
            // For multiple segments, chain them together as a degree-2 NURBS
            if (n_segs == 1) {
                double half_angle = seg_angle / 2.0;
                double ch = std::cos(half_angle);
                double sh = std::sin(half_angle);

                Point3 P0 = pt0;
                Point3 P2 = origin + diag * std::cos(seg_angle) + y_axis * std::sin(seg_angle);
                Point3 P1 = origin + diag + y_axis * (sh / ch);

                std::vector<Vector4> ctrl_pts;
                ctrl_pts.push_back(Vector4(P0, 1.0));
                ctrl_pts.push_back(Vector4(P1 * ch, ch));
                ctrl_pts.push_back(Vector4(P2, 1.0));

                std::vector<double> knots = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};

                auto bspline = geometry::BSplineCurve<Vector4>(
                    geometry::KnotVec(std::move(knots)), std::move(ctrl_pts));
                return Curve(geometry::NurbsCurve(std::move(bspline)));
            } else {
                // Multi-segment arc: n_segs quadratic NURBS arcs pieced together
                std::vector<Vector4> ctrl_pts;

                for (size_t seg = 0; seg < n_segs; ++seg) {
                    double half_seg = seg_angle / 2.0;
                    double ch = std::cos(half_seg);
                    double sh = std::sin(half_seg);
                    double wt = ch;

                    double t_start = static_cast<double>(seg) * seg_angle;
                    double t_end = (static_cast<double>(seg) + 1.0) * seg_angle;

                    Point3 P_start = origin + diag * std::cos(t_start) + y_axis * std::sin(t_start);
                    Point3 P_end = origin + diag * std::cos(t_end) + y_axis * std::sin(t_end);

                    Vector3 diag_rot = diag * std::cos(t_start) + y_axis * std::sin(t_start);
                    Vector3 y_rot = glm::cross(axis, diag_rot);
                    Point3 P1 = origin + diag_rot + y_rot * (sh / ch);

                    if (seg == 0) {
                        ctrl_pts.push_back(Vector4(P_start, 1.0));
                    }
                    ctrl_pts.push_back(Vector4(P1 * wt, wt));
                    ctrl_pts.push_back(Vector4(P_end, 1.0));
                }

                std::vector<double> knots;
                for (size_t i = 0; i < 3; ++i) knots.push_back(0.0);
                for (size_t i = 1; i < n_segs; ++i) {
                    knots.push_back(static_cast<double>(i) / static_cast<double>(n_segs));
                }
                for (size_t i = 0; i < 3; ++i) knots.push_back(1.0);

                auto bspline = geometry::BSplineCurve<Vector4>(
                    geometry::KnotVec(std::move(knots)), std::move(ctrl_pts));
                return Curve(geometry::NurbsCurve(std::move(bspline)));
            }
        }

        static inline Curve makeLineCurve(const Point3& p0, const Point3& p1) {
            return Curve(geometry::Line<Point3>(p0, p1));
        }

        static inline Surface makePlaneSurface(const Point3& origin, const Vector3& u, const Vector3& v) {
            return Surface(geometry::Plane(origin, u, v));
        }

        static inline Surface makeExtrudedSurface(const Curve& curve, const Vector3& vec) {
            if (curve.holds<geometry::Line<Point3>>()) {
                auto& line = curve.get<geometry::Line<Point3>>();
                Point3 p0 = line.frontPoint();
                Point3 p1 = line.backPoint();
                return Surface(geometry::Plane(p0, p1 - p0, vec));
            }
            auto ext = geometry::ExtrudedCurve<Curve>(curve, vec);
            return Surface(geometry::Processor<geometry::ExtrudedCurve<Curve>, Matrix4>(std::move(ext), Matrix4(1.0)));
        }

        static inline Surface makeRevolutedSurface(const Curve& curve, const Point3& origin, const Vector3& axis) {
            auto rev = geometry::RevolutedCurve<Curve>(curve, origin, axis);
            return Surface(geometry::Processor<geometry::RevolutedCurve<Curve>, Matrix4>(std::move(rev), Matrix4(1.0)));
        }

        using EdgeMap = std::unordered_map<VertexID<Point3>, Edge<Point3, Curve>, typename VertexID<Point3>::Hash>;

        static inline Edge<Point3, Curve> connectVertices(
            const Vertex<Point3>& v0,
            const Vertex<Point3>& v1,
            const std::function<Curve(const Point3&, const Point3&)>& connectPoints)
        {
            Curve c = connectPoints(v0.point(), v1.point());
            return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
        }

        static inline Face<Point3, Curve, Surface> connectEdges(
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

        static inline Face<Point3, Curve, Surface> subConnectWires(
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

        static inline Shell<Point3, Curve, Surface> connectWires(
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

        static inline Shell<Point3, Curve, Surface> connectEdgeVectors(
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

        static inline Surface makeHomotopySurface(const Curve& c0, const Curve& c1) {
            using namespace geometry;

            if (c0.holds<Line<Point3>>() && c1.holds<Line<Point3>>()) {
                Point3 p00 = curve_subs(c0, curve_rangeTuple(c0).first);
                Point3 p01 = curve_subs(c0, curve_rangeTuple(c0).second);
                Point3 p10 = curve_subs(c1, curve_rangeTuple(c1).first);
                return Surface(Plane(p00, p01 - p00, p10 - p00));
            }

            auto h0 = c0.liftUp();
            auto h1 = c1.liftUp();

            if (h0.controlPoints().size() == h1.controlPoints().size() &&
                h0.knotVec().len() == h1.knotVec().len()) {
                auto surf = BSplineSurface<Vector4>::homotopy(h0, h1);
                return Surface(NurbsSurface(std::move(surf)));
            }

            auto [div0, pts0] = c0.parameterDivision(c0.rangeTuple(), TOLERANCE);
            auto [div1, pts1] = c1.parameterDivision(c1.rangeTuple(), TOLERANCE);
            size_t n = std::min(pts0.size(), pts1.size());

            std::vector<std::vector<Vector4>> surf_cps;
            surf_cps.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                surf_cps.push_back({Vector4(pts0[i], 1.0), Vector4(pts1[i], 1.0)});
            }

            std::vector<double> u_knots_vec;
            u_knots_vec.reserve(n + 2);
            u_knots_vec.push_back(div0.front());
            for (size_t i = 0; i < n; ++i) {
                u_knots_vec.push_back(div0[i]);
            }
            u_knots_vec.push_back(div0.back());

            KnotVec u_kv(std::move(u_knots_vec));
            KnotVec v_kv = KnotVec::bezier_knot(1);

            return Surface(NurbsSurface(BSplineSurface<Vector4>(
                {std::move(u_kv), std::move(v_kv)}, std::move(surf_cps))));
        }

        static inline Vector3 takeOneAxisByNormal(Vector3 n) {
            Vector3 a = glm::abs(n);
            if (a.x > a.z || a.y > a.z)
                return glm::normalize(Vector3(-n.y, n.x, 0.0));
            return glm::normalize(Vector3(-n.z, 0.0, n.x));
        }

        static inline std::optional<geometry::Plane> attachPlane(const std::vector<std::vector<Point3>>& pts) {
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

            if (geometry::soSmall(glm::length(normal))) return std::nullopt;
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

            geometry::BoundingBox3D bbox;
            for (const auto& wire_pts : tpts)
                for (const auto& p : wire_pts)
                    bbox.push(p);

            Vector3 diag = bbox.diagonal();
            if (!geometry::soSmall(diag.z)) return std::nullopt;

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

            geometry::Plane plane(minPt, Point3(maxPt.x, minPt.y, minPt.z), Point3(minPt.x, maxPt.y, minPt.z));
            plane.transformBy(mat);
            return plane;
        }
    };

    // ============================================================
    // Circle arc
    // ============================================================

    static inline Edge<Point3, Curve> circleArc(
        const Vertex<Point3>& v0,
        const Vertex<Point3>& v1,
        ArcConstraintValue constraint)
    {
        Point3 pt0 = v0.point();
        Point3 pt1 = v1.point();

        if (constraint.kind == ArcConstraint::Transit) {
            Point3 origin = Detail::circumCenter(pt0, pt1, constraint.transit);
            Vector3 vec0 = pt0 - constraint.transit;
            Vector3 vec1 = pt1 - constraint.transit;
            Vector3 axis = glm::normalize(glm::cross(vec1, vec0));
            double angle = 2.0 * BREP_PI - glm::acos(glm::clamp(
                glm::dot(vec0, vec1) / (glm::length(vec0) * glm::length(vec1)), -1.0, 1.0));
            Curve c = Detail::makeArcCurve(pt0, origin, axis, angle);
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

            Curve c = Detail::makeArcCurve(pt0, origin, axis, angle);
            return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
        }
    }

    // ============================================================
    // Bezier curve
    // ============================================================

    static inline Edge<Point3, Curve> bezier(
        const Vertex<Point3>& v0,
        const Vertex<Point3>& v1,
        std::vector<Point3> inter_points)
    {
        std::vector<Point3> ctrl_pts;
        ctrl_pts.push_back(v0.point());
        for (auto& p : inter_points) ctrl_pts.push_back(std::move(p));
        ctrl_pts.push_back(v1.point());

        size_t degree = ctrl_pts.size() - 1;
        geometry::KnotVec kv = geometry::KnotVec::bezier_knot(degree);
        auto curve = geometry::BSplineCurve<Point3>(std::move(kv), std::move(ctrl_pts));
        return Edge<Point3, Curve>::newUnchecked(v0, v1, Curve(std::move(curve)));
    }

    // ============================================================
    // Homotopy
    // ============================================================

    static inline Face<Point3, Curve, Surface> homotopy(
        const Edge<Point3, Curve>& edge0,
        const Edge<Point3, Curve>& edge1)
    {
        // Wire 边序 (闭合):
        // edge0:              edge0.front → edge0.back
        // edge_right:         edge0.back  → edge1.back
        // edge1.inverse():    edge1.back  → edge1.front
        // edge_left.inverse():edge1.front → edge0.front
        auto edge_right = line(edge0.back(), edge1.back());
        auto edge_left = line(edge0.front(), edge1.front());

        std::deque<Edge<Point3, Curve>> wire_edges;
        wire_edges.push_back(edge0);
        wire_edges.push_back(std::move(edge_right));
        wire_edges.push_back(edge1.inverse());
        wire_edges.push_back(std::move(edge_left).inverse());
        auto wire = Wire<Point3, Curve>::newUnchecked(std::move(wire_edges));

        Surface surface = Detail::makeHomotopySurface(edge0.orientedCurve(), edge1.orientedCurve());
        return Face<Point3, Curve, Surface>::newUnchecked({std::move(wire)}, std::move(surface));
    }

    static inline core::Result<Shell<Point3, Curve, Surface>> tryWireHomotopy(
        const Wire<Point3, Curve>& wire0,
        const Wire<Point3, Curve>& wire1)
    {
        if (wire0.len() != wire1.len()) {
            return core::Err<Shell<Point3, Curve, Surface>>(
                core::Error::make(static_cast<int32_t>(7), "wires have different number of edges"));
        }

        auto connectPoints = [](const Point3& p0, const Point3& p1) -> Curve {
            return Detail::makeLineCurve(p0, p1);
        };
        auto connectCurves = [](const Curve& c0, const Curve& c1) -> Surface {
            return Detail::makeHomotopySurface(c0, c1);
        };

        Shell<Point3, Curve, Surface> shell = Detail::connectWires(wire0, wire1, connectPoints, connectCurves);
        return core::Ok(std::move(shell));
    }

    // ============================================================
    // Attach plane
    // ============================================================

    static inline core::Result<Face<Point3, Curve, Surface>> tryAttachPlane(
        const std::vector<Wire<Point3, Curve>>& wires)
    {
        auto face_result = Face<Point3, Curve, Surface>::tryNew(
            std::vector<Wire<Point3, Curve>>(wires), geometry::Plane());
        if (!face_result) return core::Err<Face<Point3, Curve, Surface>>(face_result.error());

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

        auto plane_opt = Detail::attachPlane(pts);
        if (!plane_opt)
            return core::Err<Face<Point3, Curve, Surface>>(
                core::Error::make(2, "wires are not in one plane"));

        return core::Ok(Face<Point3, Curve, Surface>::newUnchecked(
            std::vector<Wire<Point3, Curve>>(wires), Surface(std::move(*plane_opt))));
    }

    // ============================================================
    // Clone / Transform
    // ============================================================

    static inline Vertex<Point3> clone(const Vertex<Point3>& v) { return Vertex<Point3>(v.point()); }

    static inline Edge<Point3, Curve> clone(const Edge<Point3, Curve>& e) { return e.absoluteClone(); }

    static inline Wire<Point3, Curve> clone(const Wire<Point3, Curve>& w) {
        std::deque<Edge<Point3, Curve>> edges;
        for (const auto& e : w.edges()) edges.push_back(clone(e));
        return Wire<Point3, Curve>::newUnchecked(std::move(edges));
    }

    static inline Face<Point3, Curve, Surface> clone(const Face<Point3, Curve, Surface>& f) {
        std::vector<Wire<Point3, Curve>> bds;
        for (const auto& w : f.boundaries()) bds.push_back(clone(w));
        auto face = Face<Point3, Curve, Surface>::newUnchecked(std::move(bds), f.surface());
        if (!f.orientation()) face.invert();
        return face;
    }

    static inline Vertex<Point3> transformed(const Vertex<Point3>& v, const Matrix4& mat) {
        return Vertex<Point3>(Point3(mat * glm::dvec4(v.point(), 1.0)));
    }

    static inline Edge<Point3, Curve> transformed(const Edge<Point3, Curve>& e, const Matrix4& mat) {
        auto v0 = transformed(e.absoluteFront(), mat);
        auto v1 = transformed(e.absoluteBack(), mat);
        Curve c = e.curve();
        curve_transformBy(c, mat);
        return Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c));
    }

    static inline Wire<Point3, Curve> transformed(const Wire<Point3, Curve>& w, const Matrix4& mat) {
        std::deque<Edge<Point3, Curve>> edges;
        for (const auto& e : w.edges()) edges.push_back(transformed(e, mat));
        return Wire<Point3, Curve>::newUnchecked(std::move(edges));
    }

    static inline Face<Point3, Curve, Surface> transformed(const Face<Point3, Curve, Surface>& f, const Matrix4& mat) {
        std::vector<Wire<Point3, Curve>> bds;
        for (const auto& w : f.boundaries()) bds.push_back(transformed(w, mat));
        Surface s = f.surface();
        surface_transformBy(s, mat);
        auto face = Face<Point3, Curve, Surface>::newUnchecked(std::move(bds), std::move(s));
        if (!f.orientation()) face.invert();
        return face;
    }

    static inline Shell<Point3, Curve, Surface> transformed(const Shell<Point3, Curve, Surface>& sh, const Matrix4& mat) {
        Shell<Point3, Curve, Surface> result;
        for (size_t i = 0; i < sh.len(); ++i) result.push(transformed(sh[i], mat));
        return result;
    }

    static inline Solid<Point3, Curve, Surface> transformed(const Solid<Point3, Curve, Surface>& solid, const Matrix4& mat) {
        std::vector<Shell<Point3, Curve, Surface>> bds;
        for (const auto& sh : solid.boundaries()) bds.push_back(transformed(sh, mat));
        return Solid<Point3, Curve, Surface>::newUnchecked(std::move(bds));
    }

    static inline Vertex<Point3> translated(const Vertex<Point3>& v, Vector3 vec) {
        return transformed(v, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
    }
    static inline Edge<Point3, Curve> translated(const Edge<Point3, Curve>& e, Vector3 vec) {
        return transformed(e, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
    }
    static inline Wire<Point3, Curve> translated(const Wire<Point3, Curve>& w, Vector3 vec) {
        return transformed(w, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
    }
    static inline Face<Point3, Curve, Surface> translated(const Face<Point3, Curve, Surface>& f, Vector3 vec) {
        return transformed(f, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
    }
    static inline Shell<Point3, Curve, Surface> translated(const Shell<Point3, Curve, Surface>& sh, Vector3 vec) {
        return transformed(sh, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
    }
    static inline Solid<Point3, Curve, Surface> translated(const Solid<Point3, Curve, Surface>& s, Vector3 vec) {
        return transformed(s, Matrix4(glm::translate(glm::dmat4(1.0), vec)));
    }

    static inline Matrix4 rotationMatrix(Point3 origin, Vector3 axis, double angle) {
        auto m0 = Matrix4(glm::translate(glm::dmat4(1.0), -origin));
        auto m1 = Matrix4(glm::rotate(glm::dmat4(1.0), angle, axis));
        auto m2 = Matrix4(glm::translate(glm::dmat4(1.0), origin));
        return m2 * m1 * m0;
    }

    static inline Matrix4 scaleMatrix(Point3 origin, Vector3 scales) {
        auto m0 = Matrix4(glm::translate(glm::dmat4(1.0), -origin));
        auto m1 = Matrix4(glm::scale(glm::dmat4(1.0), scales));
        auto m2 = Matrix4(glm::translate(glm::dmat4(1.0), origin));
        return m2 * m1 * m0;
    }

    static inline Vertex<Point3> rotated(const Vertex<Point3>& v, Point3 o, Vector3 a, double ang) { return transformed(v, rotationMatrix(o, a, ang)); }
    static inline Edge<Point3, Curve> rotated(const Edge<Point3, Curve>& e, Point3 o, Vector3 a, double ang) { return transformed(e, rotationMatrix(o, a, ang)); }
    static inline Wire<Point3, Curve> rotated(const Wire<Point3, Curve>& w, Point3 o, Vector3 a, double ang) { return transformed(w, rotationMatrix(o, a, ang)); }
    static inline Face<Point3, Curve, Surface> rotated(const Face<Point3, Curve, Surface>& f, Point3 o, Vector3 a, double ang) { return transformed(f, rotationMatrix(o, a, ang)); }
    static inline Shell<Point3, Curve, Surface> rotated(const Shell<Point3, Curve, Surface>& sh, Point3 o, Vector3 a, double ang) { return transformed(sh, rotationMatrix(o, a, ang)); }
    static inline Solid<Point3, Curve, Surface> rotated(const Solid<Point3, Curve, Surface>& s, Point3 o, Vector3 a, double ang) { return transformed(s, rotationMatrix(o, a, ang)); }

    static inline Vertex<Point3> scaled(const Vertex<Point3>& v, Point3 o, Vector3 s) { return transformed(v, scaleMatrix(o, s)); }
    static inline Edge<Point3, Curve> scaled(const Edge<Point3, Curve>& e, Point3 o, Vector3 s) { return transformed(e, scaleMatrix(o, s)); }
    static inline Wire<Point3, Curve> scaled(const Wire<Point3, Curve>& w, Point3 o, Vector3 s) { return transformed(w, scaleMatrix(o, s)); }
    static inline Face<Point3, Curve, Surface> scaled(const Face<Point3, Curve, Surface>& f, Point3 o, Vector3 s) { return transformed(f, scaleMatrix(o, s)); }

    // ============================================================
    // 拓扑去重 (dedup)
    // ============================================================

    /// 对 Shell 执行顶点和边的几何去重
    /// 将几何相同（容差内）的 Vertex 统一为同一个实例，
    /// 并将 (front, back, curve) 相同的 Edge 统一。
    static inline Shell<Point3, Curve, Surface> dedup(
        const Shell<Point3, Curve, Surface>& shell)
    {
        struct VertexHasher {
            size_t operator()(const Vertex<Point3>& v) const {
                auto p = v.point();
                return std::hash<double>{}(p.x) ^
                       (std::hash<double>{}(p.y) << 1) ^
                       (std::hash<double>{}(p.z) << 2);
            }
        };
        struct VertexEq {
            bool operator()(const Vertex<Point3>& a, const Vertex<Point3>& b) const {
                return geometry::near(a.point(), b.point());
            }
        };
        struct EdgeHasher {
            size_t operator()(const Edge<Point3, Curve>& e) const {
                auto fp = e.front().point(), bp = e.back().point();
                return std::hash<double>{}(fp.x) ^ (std::hash<double>{}(fp.y) << 1) ^
                       (std::hash<double>{}(fp.z) << 2) ^
                       (std::hash<double>{}(bp.x) << 3) ^ (std::hash<double>{}(bp.y) << 4) ^
                       (std::hash<double>{}(bp.z) << 5);
            }
        };
        struct EdgeEq {
            bool operator()(const Edge<Point3, Curve>& a,
                            const Edge<Point3, Curve>& b) const {
                bool forward = geometry::near(a.front().point(), b.front().point()) &&
                               geometry::near(a.back().point(), b.back().point());
                bool reverse = geometry::near(a.front().point(), b.back().point()) &&
                               geometry::near(a.back().point(), b.front().point());
                return forward || reverse;
            }
        };

        std::unordered_map<Vertex<Point3>, Vertex<Point3>, VertexHasher, VertexEq> vmap;

        auto getCanonical = [&](const Vertex<Point3>& v) -> Vertex<Point3> {
            auto it = vmap.find(v);
            if (it != vmap.end()) return it->second;
            vmap[v] = v;
            return v;
        };

        for (size_t fi = 0; fi < shell.len(); ++fi) {
            const auto& face = shell[fi];
            for (size_t bi = 0; bi < face.numBoundaries(); ++bi) {
                for (const auto& e : face.boundary(bi).edges()) {
                    getCanonical(e.front());
                    getCanonical(e.back());
                }
            }
        }

        std::unordered_map<Edge<Point3, Curve>, Edge<Point3, Curve>,
                           EdgeHasher, EdgeEq> emap;

        auto getCanonicalEdge = [&](const Edge<Point3, Curve>& e) -> Edge<Point3, Curve> {
            auto it = emap.find(e);
            if (it != emap.end()) return it->second;
            auto cf = getCanonical(e.front());
            auto cb = getCanonical(e.back());
            auto ce = Edge<Point3, Curve>::newUnchecked(cf, cb, e.curve());
            emap[e] = ce;
            return ce;
        };

        Shell<Point3, Curve, Surface> result;
        for (size_t fi = 0; fi < shell.len(); ++fi) {
            const auto& face = shell[fi];
            std::vector<Wire<Point3, Curve>> new_bds;
            for (size_t bi = 0; bi < face.numBoundaries(); ++bi) {
                std::deque<Edge<Point3, Curve>> new_edges;
                for (const auto& e : face.boundary(bi).edges()) {
                    new_edges.push_back(getCanonicalEdge(e));
                }
                new_bds.push_back(Wire<Point3, Curve>::newUnchecked(std::move(new_edges)));
            }
            Surface s = face.surface();
            auto new_face = Face<Point3, Curve, Surface>::newUnchecked(
                std::move(new_bds), std::move(s));
            if (!face.orientation()) new_face.invert();
            result.push(std::move(new_face));
        }

        return result;
    }

    /// 对 Solid 执行去重
    static inline Solid<Point3, Curve, Surface> dedup(
        const Solid<Point3, Curve, Surface>& solid)
    {
        std::vector<Shell<Point3, Curve, Surface>> bds;
        for (const auto& sh : solid.boundaries()) {
            bds.push_back(dedup(sh));
        }
        return Solid<Point3, Curve, Surface>::newUnchecked(std::move(bds));
    }
};

} // namespace mulan::BRep
