#include "truck_shape_ops.h"
#include "truck_shape_storage.h"

#include <mulan/core/result/error.h>

#include "truck_bridge_c.h"

#include <array>
#include <cmath>
#include <vector>

namespace mulan::modeling {
namespace {

struct VertexHandle {
    TruckVertex* ptr = nullptr;
    VertexHandle() = default;
    explicit VertexHandle(TruckVertex* vertex) : ptr(vertex) {}
    VertexHandle(const VertexHandle&) = delete;
    VertexHandle& operator=(const VertexHandle&) = delete;
    VertexHandle(VertexHandle&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    VertexHandle& operator=(VertexHandle&& other) noexcept {
        if (this != &other) {
            if (ptr)
                truck_vertex_free(ptr);
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }
    ~VertexHandle() {
        if (ptr)
            truck_vertex_free(ptr);
    }
};

void freeError(TruckError* err) {
    if (err)
        truck_error_free(err);
}

std::string errorMessage(TruckError* err, const char* fallback) {
    if (!err)
        return fallback;
    TruckStr msg = truck_error_message(err);
    std::string result = fallback;
    if (msg.ptr && msg.len > 0)
        result.assign(reinterpret_cast<const char*>(msg.ptr), static_cast<size_t>(msg.len));
    truck_str_free(msg);
    truck_error_free(err);
    return result;
}

core::Result<const TruckSolid*> solidFromShape(const Shape& shape) {
    auto storage = storageOf(shape);
    if (!storage)
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "truck operand is empty Shape"));

    auto truck = std::dynamic_pointer_cast<TruckShapeStorage>(storage);
    if (!truck || !truck->solid()) {
        return std::unexpected(core::Error::make(core::ErrorCode::NotSupported, "operand is not a truck shape"));
    }
    return truck->solid();
}

core::Result<Shape> shapeFromAbstractSolid(AbstractShape* abstractShape, const char* context) {
    if (!abstractShape)
        return std::unexpected(
                core::Error::make(core::ErrorCode::Internal, std::string(context) + ": null abstract shape"));

    TruckSolid* solid = truck_abstractshape_into_solid(abstractShape);
    if (!solid) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::Internal, std::string(context) + ": result is not a solid"));
    }
    return makeTruckShape(solid);
}

math::Vec3 normalizedDirection(const ExtrudeParams& params) {
    math::Vec3 dir = params.direction;
    if (dir.x == 0.0 && dir.y == 0.0 && dir.z == 0.0)
        dir = params.profile.frame.normal;
    if (params.inward)
        dir = -dir;
    return dir;
}

bool finitePositive(double value) {
    return std::isfinite(value) && value > 0.0;
}

}  // namespace

core::Result<Shape> TruckShapeOps::extrude(const ExtrudeParams& params) {
    if (params.circleProfile) {
        return std::unexpected(core::Error::make(core::ErrorCode::NotSupported,
                                                 "truck extrude does not support analytic circle profiles yet"));
    }
    if (!params.profile.hasOuterLoop())
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "extrude profile has no outer loop"));
    if (!finitePositive(params.distance))
        return std::unexpected(core::Error::make(core::ErrorCode::InvalidArg, "extrude distance must be positive"));

    const auto& points = params.profile.outer.points;
    if (points.size() < 3)
        return std::unexpected(
                core::Error::make(core::ErrorCode::InvalidArg, "truck extrude requires at least 3 profile points"));

    std::vector<VertexHandle> vertices;
    vertices.reserve(points.size());
    for (const auto& p : points) {
        VertexHandle v(truck_vertex_new(p.x, p.y, p.z));
        if (!v.ptr)
            return std::unexpected(
                    core::Error::make(core::ErrorCode::Internal, "truck failed to create profile vertex"));
        vertices.push_back(std::move(v));
    }

    std::vector<TruckEdge*> edges;
    edges.reserve(points.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        TruckVertex* a = vertices[i].ptr;
        TruckVertex* b = vertices[(i + 1) % vertices.size()].ptr;
        TruckEdge* edge = truck_edge_line(a, b);
        if (!edge) {
            for (TruckEdge* e : edges)
                truck_edge_free(e);
            return std::unexpected(core::Error::make(core::ErrorCode::Internal, "truck failed to create profile edge"));
        }
        edges.push_back(edge);
    }

    TruckWire* wire = truck_wire_from_edges(edges.data(), edges.size());
    if (!wire) {
        for (TruckEdge* e : edges)
            truck_edge_free(e);
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "truck failed to create profile wire"));
    }

    TruckFace* face = truck_face_attach_plane(wire);
    truck_wire_free(wire);
    if (!face)
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "truck failed to create profile face"));

    AbstractShape* source = truck_face_upcast(face);
    if (!source) {
        truck_face_free(face);
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "truck failed to upcast profile face"));
    }

    const math::Vec3 dir = normalizedDirection(params);
    const std::array<double, 3> sweep{ dir.x * params.distance, dir.y * params.distance, dir.z * params.distance };
    AbstractShape* swept = nullptr;
    TruckError* err = nullptr;
    const bool ok = truck_tsweep(source, sweep.data(), sweep.size(), &swept, &err);
    truck_abstractshape_free(source);
    if (!ok || !swept)
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, errorMessage(err, "truck tsweep failed")));
    freeError(err);

    return shapeFromAbstractSolid(swept, "truck extrude");
}

core::Result<Shape> TruckShapeOps::boolean(const Shape& target, const Shape& tool, BooleanOp op) {
    auto a = solidFromShape(target);
    if (!a)
        return std::unexpected(a.error());
    auto b = solidFromShape(tool);
    if (!b)
        return std::unexpected(b.error());

    TruckSolid* result = nullptr;
    switch (op) {
    case BooleanOp::Union: result = truck_solid_or(*a, *b, 0.05); break;
    case BooleanOp::Intersection: result = truck_solid_and(*a, *b, 0.05); break;
    case BooleanOp::Difference:
        return std::unexpected(
                core::Error::make(core::ErrorCode::NotSupported,
                                  "truck boolean difference is not wired until bridge exposes cut directly"));
    }

    if (!result)
        return std::unexpected(core::Error::make(core::ErrorCode::Internal, "truck boolean failed"));
    return makeTruckShape(result);
}

}  // namespace mulan::modeling
