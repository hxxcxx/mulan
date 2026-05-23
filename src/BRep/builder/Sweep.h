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
#include "Builder.h"

#include <MulanGeo/Geometry/Types.h>
#include <MulanGeo/Geometry/Tolerance.h>
#include <MulanGeo/Geometry/Specified/Line.h>
#include <MulanGeo/Geometry/Specified/Plane.h>
#include <MulanGeo/Geometry/Specified/Circle.h>
#include <MulanGeo/Geometry/Nurbs/BSplineCurve.h>
#include <MulanGeo/Geometry/Decorators/RevolutedCurve.h>
#include <MulanGeo/Geometry/Decorators/ExtrudedCurve.h>

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace MulanGeo::BRep::sweep {

using Point3  = Geometry::Point3;
using Vector3 = Geometry::Vector3;
using Matrix4 = Geometry::Matrix4;

// ============================================================
// Translation sweep (tsweep)
// ============================================================

inline Edge<Point3, Curve> tsweep(const Vertex<Point3>& v, Vector3 vector) {
    auto v1 = builder::transformed(v, Matrix4(glm::translate(glm::dmat4(1.0), vector)));
    return builder::line(v, v1);
}

inline Face<Point3, Curve, Surface> tsweep(const Edge<Point3, Curve>& edge, Vector3 vector) {
    auto connectPoints = [&vector](const Point3& p0, const Point3& p1) -> Curve {
        return Curve(Geometry::Line<Point3>(p0, p1));
    };
    auto connectCurves = [&vector](const Curve& c0, const Curve& c1) -> Surface {
        return builder::detail::makeExtrudedSurface(c0, vector);
    };

    auto edge1 = builder::transformed(edge, Matrix4(glm::translate(glm::dmat4(1.0), vector)));
    return builder::detail::connectEdges(edge, edge1, connectPoints, connectCurves);
}

inline Shell<Point3, Curve, Surface> tsweep(const Wire<Point3, Curve>& wire, Vector3 vector) {
    auto connectPoints = [&vector](const Point3& p0, const Point3& p1) -> Curve {
        return Curve(Geometry::Line<Point3>(p0, p1));
    };
    auto connectCurves = [&vector](const Curve& c0, const Curve& c1) -> Surface {
        return builder::detail::makeExtrudedSurface(c0, vector);
    };

    auto wire1 = builder::transformed(wire, Matrix4(glm::translate(glm::dmat4(1.0), vector)));
    return builder::detail::connectWires(wire, wire1, connectPoints, connectCurves);
}

inline Solid<Point3, Curve, Surface> tsweep(const Face<Point3, Curve, Surface>& face, Vector3 vector) {
    auto mat = Matrix4(glm::translate(glm::dmat4(1.0), vector));

    auto connectPoints = [&vector](const Point3& p0, const Point3& p1) -> Curve {
        return Curve(Geometry::Line<Point3>(p0, p1));
    };
    auto connectCurves = [&vector](const Curve& c0, const Curve& c1) -> Surface {
        return builder::detail::makeExtrudedSurface(c0, vector);
    };

    Shell<Point3, Curve, Surface> shell;
    shell.push(face.inverse());

    auto top_face = builder::transformed(face, mat);
    for (size_t bi = 0; bi < face.boundaries().size() && bi < top_face.boundaries().size(); ++bi) {
        const auto& b0 = face.boundary(bi);
        const auto& b1 = top_face.boundary(bi);
        auto connected = builder::detail::connectWires(b0, b1, connectPoints, connectCurves);
        for (size_t j = 0; j < connected.len(); ++j)
            shell.push(std::move(connected[j]));
    }
    shell.push(std::move(top_face));

    return Solid<Point3, Curve, Surface>::newUnchecked({std::move(shell)});
}

// ============================================================
// Rotation sweep (rsweep)
// ============================================================

inline Wire<Point3, Curve> rsweepVertex(
    const Vertex<Point3>& v, Point3 origin, Vector3 axis, double angle, size_t division)
{
    if (division <= 1) {
        double step = angle / static_cast<double>(std::max(division, size_t(1)));
        auto trsl = builder::rotationMatrix(origin, axis, step);
        auto next = builder::transformed(v, trsl);
        std::deque<Edge<Point3, Curve>> edges;
        edges.push_back(builder::circleArc(v, next,
            BRep::builder::ArcConstraintValue::fromTangent(glm::cross(axis, next.point() - origin))));
        return Wire<Point3, Curve>::newUnchecked(std::move(edges));
    }

    double step = angle / static_cast<double>(division);
    auto trsl = builder::rotationMatrix(origin, axis, step);

    std::deque<Edge<Point3, Curve>> edges;
    auto current = v;
    for (size_t i = 0; i < division; ++i) {
        auto next = builder::transformed(current, trsl);
        edges.push_back(builder::circleArc(current, next,
            BRep::builder::ArcConstraintValue::fromTangent(glm::cross(axis, next.point() - origin))));
        current = next;
    }

    return Wire<Point3, Curve>::newUnchecked(std::move(edges));
}

inline Wire<Point3, Curve> rsweepClosed(
    const Vertex<Point3>& v, Point3 origin, Vector3 axis, size_t division)
{
    double angle = 2.0 * builder::BREP_PI;
    double step = angle / static_cast<double>(division);
    auto trsl = builder::rotationMatrix(origin, axis, step);

    std::deque<Edge<Point3, Curve>> edges;
    auto current = v;
    for (size_t i = 0; i < division; ++i) {
        auto next = builder::transformed(current, trsl);
        edges.push_back(builder::circleArc(current, next,
            builder::ArcConstraintValue::fromTangent(glm::cross(axis, next.point() - origin))));
        current = next;
    }
    // Close the loop
    edges.push_back(builder::circleArc(current, v,
        builder::ArcConstraintValue::fromTangent(glm::cross(axis, v.point() - origin))));

    return Wire<Point3, Curve>::newUnchecked(std::move(edges));
}

inline Wire<Point3, Curve> rsweepVertexPartial(
    const Vertex<Point3>& v, Point3 origin, Vector3 axis, double angle, size_t division)
{
    double step = angle / static_cast<double>(division);
    auto trsl = builder::rotationMatrix(origin, axis, step);

    std::deque<Edge<Point3, Curve>> edges;
    auto current = v;
    for (size_t i = 0; i < division; ++i) {
        auto next = builder::transformed(current, trsl);
        edges.push_back(builder::circleArc(current, next,
            builder::ArcConstraintValue::fromTangent(glm::cross(axis, next.point() - origin))));
        current = next;
    }

    return Wire<Point3, Curve>::newUnchecked(std::move(edges));
}

inline Shell<Point3, Curve, Surface> rsweep(
    const Edge<Point3, Curve>& edge, Point3 origin, Vector3 axis, double angle, size_t division)
{
    auto connectPoints = [origin, axis](const Point3& p0, const Point3& p1) -> Curve {
        Point3 proj = origin + glm::dot(p0 - origin, axis) * axis;
        Vector3 r = p0 - proj;
        if (Geometry::soSmall(glm::length(r)))
            return Curve(Geometry::Line<Point3>(p0, p1));
        return builder::detail::makeArcCurve(p0, proj, axis, 0.0); // step is set by caller
    };

    auto connectCurves = [origin, axis](const Curve& c0, const Curve& c1) -> Surface {
        return builder::detail::makeRevolutedSurface(c0, origin, axis);
    };

    if (angle >= 2.0 * builder::BREP_PI - 1e-10) {
        // Closed sweep
        double step = 2.0 * builder::BREP_PI / static_cast<double>(division);
        auto trsl = builder::rotationMatrix(origin, axis, step);

        auto current = edge;
        Shell<Point3, Curve, Surface> shell;
        for (size_t i = 0; i < division; ++i) {
            auto next_edge = builder::transformed(current, trsl);
            auto face = builder::detail::connectEdges(current, next_edge,
                [origin, axis, step](const Point3& p0, const Point3& p1) -> Curve {
                    Point3 proj = origin + glm::dot(p0 - origin, axis) * axis;
                    Vector3 r = p0 - proj;
                    if (Geometry::soSmall(glm::length(r)))
                        return Curve(Geometry::Line<Point3>(p0, p1));
                    return builder::detail::makeArcCurve(p0, proj, axis, step);
                },
                [origin, axis](const Curve& c0, const Curve& c1) -> Surface {
                    return builder::detail::makeRevolutedSurface(c0, origin, axis);
                });
            shell.push(std::move(face));
            current = std::move(next_edge);
        }
        // Close the loop
        auto close_face = builder::detail::connectEdges(current, edge,
            [origin, axis, step](const Point3& p0, const Point3& p1) -> Curve {
                Point3 proj = origin + glm::dot(p0 - origin, axis) * axis;
                Vector3 r = p0 - proj;
                if (Geometry::soSmall(glm::length(r)))
                    return Curve(Geometry::Line<Point3>(p0, p1));
                return builder::detail::makeArcCurve(p0, proj, axis, step);
            },
            [origin, axis](const Curve& c0, const Curve& c1) -> Surface {
                return builder::detail::makeRevolutedSurface(c0, origin, axis);
            });
        shell.push(std::move(close_face));
        return shell;
    } else {
        // Partial sweep
        double step = angle / static_cast<double>(division);
        auto trsl = builder::rotationMatrix(origin, axis, step);

        auto current = edge;
        Shell<Point3, Curve, Surface> shell;
        for (size_t i = 0; i < division; ++i) {
            auto next_edge = builder::transformed(current, trsl);
            auto face = builder::detail::connectEdges(current, next_edge,
                [origin, axis, step](const Point3& p0, const Point3& p1) -> Curve {
                    Point3 proj = origin + glm::dot(p0 - origin, axis) * axis;
                    Vector3 r = p0 - proj;
                    if (Geometry::soSmall(glm::length(r)))
                        return Curve(Geometry::Line<Point3>(p0, p1));
                    return builder::detail::makeArcCurve(p0, proj, axis, step);
                },
                [origin, axis](const Curve& c0, const Curve& c1) -> Surface {
                    return builder::detail::makeRevolutedSurface(c0, origin, axis);
                });
            shell.push(std::move(face));
            current = std::move(next_edge);
        }
        return shell;
    }
}

inline Shell<Point3, Curve, Surface> rsweep(
    const Wire<Point3, Curve>& wire, Point3 origin, Vector3 axis, double angle, size_t division)
{
    auto makeArcConnector = [origin, axis](double step) {
        return [origin, axis, step](const Point3& p0, const Point3& p1) -> Curve {
            Point3 proj = origin + glm::dot(p0 - origin, axis) * axis;
            Vector3 r = p0 - proj;
            if (Geometry::soSmall(glm::length(r)))
                return Curve(Geometry::Line<Point3>(p0, p1));
            return builder::detail::makeArcCurve(p0, proj, axis, step);
        };
    };

    auto revoluteConnector = [origin, axis](const Curve& c0, const Curve& c1) -> Surface {
        return builder::detail::makeRevolutedSurface(c0, origin, axis);
    };

    if (angle >= 2.0 * builder::BREP_PI - 1e-10) {
        // Closed sweep
        double step = 2.0 * builder::BREP_PI / static_cast<double>(division);
        auto trsl = builder::rotationMatrix(origin, axis, step);
        auto arcConnector = makeArcConnector(step);

        auto current = wire;
        Shell<Point3, Curve, Surface> shell;
        for (size_t i = 0; i < division; ++i) {
            auto next_wire = builder::transformed(current, trsl);
            auto faces = builder::detail::connectWires(current, next_wire, arcConnector, revoluteConnector);
            for (size_t j = 0; j < faces.len(); ++j)
                shell.push(std::move(faces[j]));
            current = std::move(next_wire);
        }
        // Close the loop
        auto close_faces = builder::detail::connectWires(current, wire, arcConnector, revoluteConnector);
        for (size_t j = 0; j < close_faces.len(); ++j)
            shell.push(std::move(close_faces[j]));
        return shell;
    } else {
        // Partial sweep
        double step = angle / static_cast<double>(division);
        auto trsl = builder::rotationMatrix(origin, axis, step);
        auto arcConnector = makeArcConnector(step);

        auto current = wire;
        Shell<Point3, Curve, Surface> shell;
        for (size_t i = 0; i < division; ++i) {
            auto next_wire = builder::transformed(current, trsl);
            auto faces = builder::detail::connectWires(current, next_wire, arcConnector, revoluteConnector);
            for (size_t j = 0; j < faces.len(); ++j)
                shell.push(std::move(faces[j]));
            current = std::move(next_wire);
        }
        return shell;
    }
}

inline Shell<Point3, Curve, Surface> rsweep(
    const Face<Point3, Curve, Surface>& face, Point3 origin, Vector3 axis, double angle, size_t division)
{
    double step = angle >= 2.0 * builder::BREP_PI - 1e-10
        ? 2.0 * builder::BREP_PI / static_cast<double>(division)
        : angle / static_cast<double>(division);
    auto trsl = builder::rotationMatrix(origin, axis, step);

    auto arcConnector = [origin, axis, step](const Point3& p0, const Point3& p1) -> Curve {
        Point3 proj = origin + glm::dot(p0 - origin, axis) * axis;
        Vector3 r = p0 - proj;
        if (Geometry::soSmall(glm::length(r)))
            return Curve(Geometry::Line<Point3>(p0, p1));
        return builder::detail::makeArcCurve(p0, proj, axis, step);
    };

    auto revoluteConnector = [origin, axis](const Curve& c0, const Curve& c1) -> Surface {
        return builder::detail::makeRevolutedSurface(c0, origin, axis);
    };

    Shell<Point3, Curve, Surface> shell;

    if (angle >= 2.0 * builder::BREP_PI - 1e-10) {
        // Closed sweep
        auto current = face;
        for (size_t i = 0; i < division; ++i) {
            auto next_face = builder::transformed(current, trsl);
            for (size_t bi = 0; bi < current.boundaries().size() && bi < next_face.boundaries().size(); ++bi) {
                const auto& b0 = current.boundary(bi);
                const auto& b1 = next_face.boundary(bi);
                auto connected = builder::detail::connectWires(b0, b1, arcConnector, revoluteConnector);
                for (size_t j = 0; j < connected.len(); ++j)
                    shell.push(std::move(connected[j]));
            }
            current = std::move(next_face);
        }
    } else {
        // Partial sweep with bottom and top caps
        auto bottom = face.inverse();
        shell.push(bottom);
        auto current = face;
        for (size_t i = 0; i < division; ++i) {
            auto next_face = builder::transformed(current, trsl);
            for (size_t bi = 0; bi < current.boundaries().size() && bi < next_face.boundaries().size(); ++bi) {
                const auto& b0 = current.boundary(bi);
                const auto& b1 = next_face.boundary(bi);
                auto connected = builder::detail::connectWires(b0, b1, arcConnector, revoluteConnector);
                for (size_t j = 0; j < connected.len(); ++j)
                    shell.push(std::move(connected[j]));
            }
            current = std::move(next_face);
        }
        shell.push(current);
    }

    return shell;
}

inline std::vector<Core::Result<Solid<Point3, Curve, Surface>>> rsweep(
    const Shell<Point3, Curve, Surface>& shell, Point3 origin, Vector3 axis, double angle, size_t division)
{
    Shell<Point3, Curve, Surface> boundary;
    for (size_t i = 0; i < shell.len(); ++i) {
        auto faces = rsweep(shell[i], origin, axis, angle, division);
        for (size_t j = 0; j < faces.len(); ++j) {
            boundary.push(std::move(faces[j]));
        }
    }

    boundary = builder::dedup(std::move(boundary));
    auto result = Solid<Point3, Curve, Surface>::tryNew({std::move(boundary)});
    std::vector<Core::Result<Solid<Point3, Curve, Surface>>> vec;
    vec.push_back(std::move(result));
    return vec;
}

// ============================================================
// 基本体素
// ============================================================

/// 圆锥/圆台
/// bottom_radius: 底面半径, top_radius: 顶面半径 (0=尖顶), height: 高度
/// 轴为 Z 轴正向
inline Core::Result<Solid<Point3, Curve, Surface>> cone(
    double bottom_radius, double top_radius, double height, size_t division = 16)
{
    Point3 origin(0.0);
    Vector3 axis(0.0, 0.0, 1.0);

    auto makePlaneFromNormal = [](const Point3& pt, const Vector3& n) -> Geometry::Plane {
        Vector3 u = std::abs(n.x) < 0.9 ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
        u = glm::normalize(glm::cross(n, u));
        Vector3 v = glm::cross(n, u);
        return Geometry::Plane(pt, u, v);
    };

    Vertex<Point3> bottom_rim(Point3(bottom_radius, 0.0, 0.0));
    Wire<Point3, Curve> bottom_circle = rsweepClosed(bottom_rim, origin, axis, division);
    Vertex<Point3> top_rim(Point3(top_radius, 0.0, height));

    std::deque<Edge<Point3, Curve>> profile_edges;
    profile_edges.push_back(Edge<Point3, Curve>::newUnchecked(
        bottom_rim, top_rim, Curve(Geometry::Line<Point3>(bottom_rim.point(), top_rim.point()))));
    Wire<Point3, Curve> profile_wire = Wire<Point3, Curve>::newUnchecked(std::move(profile_edges));
    Shell<Point3, Curve, Surface> side_shell = rsweep(profile_wire, origin, axis, 2.0 * builder::BREP_PI, division);

    Face<Point3, Curve, Surface> bottom_face = Face<Point3, Curve, Surface>::newUnchecked(
        {bottom_circle.inverse()}, Surface(makePlaneFromNormal(Point3(0.0), Vector3(0.0, 0.0, -1.0))));

    Shell<Point3, Curve, Surface> shell;
    shell.push(bottom_face);
    for (size_t i = 0; i < side_shell.len(); ++i)
        shell.push(std::move(side_shell[i]));

    if (top_radius > Geometry::TOLERANCE) {
        Wire<Point3, Curve> top_circle = rsweepClosed(top_rim, origin, axis, division);
        Face<Point3, Curve, Surface> top_face = Face<Point3, Curve, Surface>::newUnchecked(
            {top_circle.inverse()}, Surface(makePlaneFromNormal(Point3(0.0, 0.0, height), Vector3(0.0, 0.0, 1.0))));
        shell.push(std::move(top_face));
    }

    return Solid<Point3, Curve, Surface>::tryNew({std::move(shell)});
}

} // namespace MulanGeo::BRep::sweep