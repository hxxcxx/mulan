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
#include "../Builder/Builder.h"
#include "../Builder/Sweep.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Specified/Circle.h>
#include <MulanGeo/Geometry/BoundingBox.h>

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace MulanGeo::BRep {

class Primitive {
public:
    using Point3  = geometry::Point3;
    using Vector3 = geometry::Vector3;
    using Matrix4 = geometry::Matrix4;
    using BoundingBox2D = geometry::BoundingBox2D;
    using BoundingBox3D = geometry::BoundingBox3D;

    Primitive() = delete;

    // ============================================================
    // Rectangle on a Plane
    // ============================================================

    static inline Wire<Point3, Curve> rect(const BoundingBox2D& box, const geometry::Plane& plane) {
        auto [min, max] = std::make_pair(box.minPt, box.maxPt);

        auto v = Builder::vertices({
            plane.subs(min.x, min.y),
            plane.subs(max.x, min.y),
            plane.subs(max.x, max.y),
            plane.subs(min.x, max.y),
        });

        std::deque<Edge<Point3, Curve>> edges;
        edges.push_back(Builder::line(v[0], v[1]));
        edges.push_back(Builder::line(v[1], v[2]));
        edges.push_back(Builder::line(v[2], v[3]));
        edges.push_back(Builder::line(v[3], v[0]));

        return Wire<Point3, Curve>::newUnchecked(std::move(edges));
    }

    // ============================================================
    // Circle from start point + rotation axis
    // ============================================================

    static inline Wire<Point3, Curve> circle(Point3 start, Point3 origin, Vector3 axis, size_t division) {
        // Project origin onto start's axis-aligned position
        origin = origin + glm::dot(start - origin, axis) * axis;
        Vector3 radius = start - origin;
        Vector3 y_axis = glm::cross(axis, radius);

        auto mat = glm::transpose(glm::dmat4(
            glm::dvec4(radius, 0.0),
            glm::dvec4(y_axis, 0.0),
            glm::dvec4(axis, 0.0),
            glm::dvec4(origin, 1.0)));

        // Create vertices
        auto makeVertex = [&](size_t i) -> Vertex<Point3> {
            double t = 2.0 * Builder::BREP_PI * static_cast<double>(i) / static_cast<double>(division);
            Point3 p = Point3(mat * glm::dvec4(std::cos(t), std::sin(t), 0.0, 1.0));
            return Builder::vertex(p);
        };

        // Create edges as arc segments between consecutive vertices
        std::deque<Edge<Point3, Curve>> edges;
        for (size_t i = 0; i < division; ++i) {
            auto v0 = makeVertex(i);
            auto v1 = makeVertex((i + 1) % division);
            double arc_angle = 2.0 * Builder::BREP_PI / static_cast<double>(division);

            // Create arc edge from v0 to v1
            Point3 pt0 = v0.point();
            Point3 arc_origin = origin;
            Vector3 arc_axis = axis;
            Curve c = Builder::Detail::makeArcCurve(pt0, arc_origin, arc_axis, arc_angle);
            edges.push_back(Edge<Point3, Curve>::newUnchecked(v0, v1, std::move(c)));
        }

        return Wire<Point3, Curve>::newUnchecked(std::move(edges));
    }

    // ============================================================
    // Cuboid from bounding box
    // ============================================================

    static inline Solid<Point3, Curve, Surface> cuboid(const BoundingBox3D& box) {
        Point3 p = box.minPt;
        Point3 q = box.maxPt;

        auto v = Builder::vertices({
            Point3(p.x, p.y, p.z),
            Point3(q.x, p.y, p.z),
            Point3(q.x, q.y, p.z),
            Point3(p.x, q.y, p.z),
            Point3(p.x, p.y, q.z),
            Point3(q.x, p.y, q.z),
            Point3(q.x, q.y, q.z),
            Point3(p.x, q.y, q.z),
        });

        Edge<Point3, Curve> e[12] = {
            Builder::line(v[0], v[1]),
            Builder::line(v[1], v[2]),
            Builder::line(v[2], v[3]),
            Builder::line(v[3], v[0]),
            Builder::line(v[0], v[4]),
            Builder::line(v[1], v[5]),
            Builder::line(v[2], v[6]),
            Builder::line(v[3], v[7]),
            Builder::line(v[4], v[5]),
            Builder::line(v[5], v[6]),
            Builder::line(v[6], v[7]),
            Builder::line(v[7], v[4]),
        };

        // Face 0 (z = p.z, bottom) 鈥?inverted orientation
        {
            std::deque<Edge<Point3, Curve>> w0_edges;
            w0_edges.push_back(e[3].inverse());
            w0_edges.push_back(e[2].inverse());
            w0_edges.push_back(e[1].inverse());
            w0_edges.push_back(e[0].inverse());
            // This will be a separate face later
        }

        // Build all 6 faces
        Shell<Point3, Curve, Surface> shell;

        // Face 0: bottom (z = p.z) 鈥?inverted
        {
            std::deque<Edge<Point3, Curve>> w_edges;
            w_edges.push_back(e[3].inverse());
            w_edges.push_back(e[2].inverse());
            w_edges.push_back(e[1].inverse());
            w_edges.push_back(e[0].inverse());
            auto wire = Wire<Point3, Curve>::newUnchecked(std::move(w_edges));
            auto face = Face<Point3, Curve, Surface>::newUnchecked(
                {std::move(wire)},
                Surface(geometry::Plane(v[0].point(), v[3].point(), v[1].point())));
            shell.push(std::move(face));
        }

        // Face 1-4: sides
        for (size_t i = 0; i < 4; ++i) {
            std::deque<Edge<Point3, Curve>> w_edges;
            w_edges.push_back(e[i]);
            w_edges.push_back(e[i + 4]);
            w_edges.push_back(e[i + 8].inverse());
            w_edges.push_back(e[(i + 1) % 4 + 4].inverse());
            auto wire = Wire<Point3, Curve>::newUnchecked(std::move(w_edges));
            auto face = Face<Point3, Curve, Surface>::newUnchecked(
                {std::move(wire)},
                Surface(geometry::Plane(v[i].point(), v[i + 1].point(), v[i + 4].point())));
            shell.push(std::move(face));
        }

        // Face 5: top (z = q.z)
        {
            std::deque<Edge<Point3, Curve>> w_edges;
            w_edges.push_back(e[8]);
            w_edges.push_back(e[9]);
            w_edges.push_back(e[10]);
            w_edges.push_back(e[11]);
            auto wire = Wire<Point3, Curve>::newUnchecked(std::move(w_edges));
            auto face = Face<Point3, Curve, Surface>::newUnchecked(
                {std::move(wire)},
                Surface(geometry::Plane(v[4].point(), v[5].point(), v[7].point())));
            shell.push(std::move(face));
        }

        return Solid<Point3, Curve, Surface>::newUnchecked({std::move(shell)});
    }

    // ============================================================
    // Cylinder via tsweep
    // ============================================================

    static inline Solid<Point3, Curve, Surface> cylinder(double radius, double height, Point3 origin, Vector3 axis, size_t division = 16) {
        // Create a circle at the base
        Point3 start = origin + radius * Builder::Detail::takeOneAxisByNormal(axis);
        auto circle_wire = circle(start, origin, axis, division);

        // Sweep the circle along the axis direction
        Vector3 vec = height * axis;
        return Sweep::tsweep(Face<Point3, Curve, Surface>::newUnchecked(
            {circle_wire},
            Surface(geometry::Plane(
                circle_wire.frontVertex().point(),
                Builder::Detail::takeOneAxisByNormal(axis),
                axis))), vec);
    }

    // ============================================================
    // Sphere via rsweep
    // ============================================================

    static inline Shell<Point3, Curve, Surface> sphere(double radius, Point3 origin, size_t u_div = 16, size_t v_div = 16) {
        // Create a semicircle and sweep it around the Y axis
        Point3 start = origin + Vector3(radius, 0, 0);
        auto v0 = Builder::vertex(origin + Vector3(radius, 0, 0));
        auto v1 = Builder::vertex(origin - Vector3(0, radius, 0));
        auto arc = Builder::circleArc(v0, v1,
            Builder::ArcConstraintValue::fromTransit(origin + Vector3(0, 0, radius)));

        std::deque<Edge<Point3, Curve>> edges = {arc};
        auto semi_circle = Wire<Point3, Curve>::newUnchecked(std::move(edges));

        return Sweep::rsweep(semi_circle, origin, Vector3(0, 1, 0), 2.0 * Builder::BREP_PI, u_div);
    }

    // ============================================================
    // Torus via double rsweep
    // ============================================================

    static inline Solid<Point3, Curve, Surface> torus(double major_radius, double minor_radius, Point3 origin, Vector3 axis, size_t u_div = 16, size_t v_div = 16) {
        // Create a circle in a plane containing the axis
        Point3 circle_center = origin + major_radius * Builder::Detail::takeOneAxisByNormal(axis);
        auto v0 = Builder::vertex(circle_center + minor_radius * Builder::Detail::takeOneAxisByNormal(axis));
        auto circle_wire = circle(v0.point(), circle_center, axis, v_div);

        // Sweep the circle around the axis
        auto shell = Sweep::rsweep(circle_wire, origin, axis, 2.0 * Builder::BREP_PI, u_div);
        return Solid<Point3, Curve, Surface>::newUnchecked({std::move(shell)});
    }
};

} // namespace MulanGeo::BRep
