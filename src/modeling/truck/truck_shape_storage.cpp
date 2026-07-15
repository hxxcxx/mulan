#include "truck_shape_storage.h"

#include <mulan/core/result/error.h>
#include <mulan/graphics/vertex/vertex_buffer.h>

#include "truck_bridge_c.h"

#include <cmath>
#include <cstring>

namespace mulan::modeling {
namespace {

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

double tessellationTolerance(const TessellationOptions& opts) {
    return opts.linearDeflection > 0.0 ? opts.linearDeflection : 0.01;
}

Result<TruckPolygonMesh*> solidToPolygon(const TruckSolid* solid, double tolerance) {
    if (!solid)
        return std::unexpected(Error::make(ErrorCode::InvalidArg, "truck solid is null"));

    TruckPolygonMesh* mesh = nullptr;
    TruckError* err = nullptr;
    if (!truck_solid_to_polygon(solid, tolerance, &mesh, &err) || !mesh) {
        return std::unexpected(Error::make(ErrorCode::Internal, errorMessage(err, "truck tessellation failed")));
    }
    return mesh;
}

math::AABB3 boundsFromMesh(const TruckPolygonMesh* mesh) {
    if (!mesh)
        return math::AABB3::empty();

    TruckF64Array bbox{};
    if (!truck_polygonmesh_bounding_box(mesh, &bbox) || bbox.len < 6 || !bbox.ptr) {
        if (bbox.ptr)
            truck_f64array_free(bbox);
        return math::AABB3::empty();
    }

    const bool finite = std::isfinite(bbox.ptr[0]) && std::isfinite(bbox.ptr[1]) && std::isfinite(bbox.ptr[2]) &&
                        std::isfinite(bbox.ptr[3]) && std::isfinite(bbox.ptr[4]) && std::isfinite(bbox.ptr[5]);
    math::AABB3 result =
            finite ? math::AABB3({ bbox.ptr[0], bbox.ptr[1], bbox.ptr[2] }, { bbox.ptr[3], bbox.ptr[4], bbox.ptr[5] })
                   : math::AABB3::empty();
    truck_f64array_free(bbox);
    return result;
}

graphics::Mesh buildSurfaceMesh(const TruckPolygonBuffer& buffer) {
    const uintptr_t vertexCount = buffer.positions.len / 3;
    if (!buffer.positions.ptr || vertexCount == 0 || buffer.indices.len == 0)
        return {};
    if (buffer.positions.len != vertexCount * 3)
        return {};

    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::TriangleList;
    mesh.indexType = graphics::IndexType::UInt32;

    graphics::VertexBufferBuilder vb(mesh.layout, static_cast<uint32_t>(vertexCount));
    for (uintptr_t i = 0; i < vertexCount; ++i) {
        vb.setPosition(static_cast<uint32_t>(i), static_cast<float>(buffer.positions.ptr[i * 3 + 0]),
                       static_cast<float>(buffer.positions.ptr[i * 3 + 1]),
                       static_cast<float>(buffer.positions.ptr[i * 3 + 2]));

        float normal[3] = { 0.0f, 0.0f, 1.0f };
        if (buffer.normal.ptr && buffer.normal.len >= i * 3 + 3) {
            normal[0] = buffer.normal.ptr[i * 3 + 0];
            normal[1] = buffer.normal.ptr[i * 3 + 1];
            normal[2] = buffer.normal.ptr[i * 3 + 2];
        }
        vb.setNormal(static_cast<uint32_t>(i), normal[0], normal[1], normal[2]);

        float uv[2] = { 0.0f, 0.0f };
        if (buffer.uv.ptr && buffer.uv.len >= i * 2 + 2) {
            uv[0] = buffer.uv.ptr[i * 2 + 0];
            uv[1] = buffer.uv.ptr[i * 2 + 1];
        }
        vb.write(static_cast<uint32_t>(i), graphics::VertexSemantic::TexCoord0, uv);
    }

    auto vertBytes = vb.data();
    mesh.vertices.assign(vertBytes.begin(), vertBytes.end());
    mesh.indices.resize(static_cast<size_t>(buffer.indices.len) * sizeof(uint32_t));
    std::memcpy(mesh.indices.data(), buffer.indices.ptr, mesh.indices.size());
    mesh.computeBounds();
    return mesh;
}

graphics::Mesh buildTriangleWireMesh(const graphics::Mesh& surfaceMesh) {
    if (surfaceMesh.empty() || surfaceMesh.indices.empty())
        return {};

    graphics::Mesh mesh;
    mesh.layout = surfaceMesh.layout;
    mesh.vertices = surfaceMesh.vertices;
    mesh.indexType = graphics::IndexType::UInt32;
    mesh.topology = graphics::PrimitiveTopology::LineList;

    const auto* tris = reinterpret_cast<const uint32_t*>(surfaceMesh.indices.data());
    const size_t triIndexCount = surfaceMesh.indices.size() / sizeof(uint32_t);
    mesh.indices.resize(triIndexCount * 2 * sizeof(uint32_t));
    auto* lines = reinterpret_cast<uint32_t*>(mesh.indices.data());

    size_t out = 0;
    for (size_t i = 0; i + 2 < triIndexCount; i += 3) {
        const uint32_t a = tris[i + 0];
        const uint32_t b = tris[i + 1];
        const uint32_t c = tris[i + 2];
        lines[out++] = a;
        lines[out++] = b;
        lines[out++] = b;
        lines[out++] = c;
        lines[out++] = c;
        lines[out++] = a;
    }
    mesh.bounds = surfaceMesh.bounds;
    return mesh;
}

}  // namespace

TruckShapeStorage::TruckShapeStorage(TruckSolid* solid) : solid_(solid) {
}

TruckShapeStorage::~TruckShapeStorage() {
    if (solid_)
        truck_solid_free(solid_);
}

BodyKind TruckShapeStorage::bodyKind() const {
    return solid_ ? BodyKind::Solid : BodyKind::Empty;
}

math::AABB3 TruckShapeStorage::bounds() const {
    auto mesh = solidToPolygon(solid_, 0.01);
    if (!mesh)
        return math::AABB3::empty();
    math::AABB3 result = boundsFromMesh(*mesh);
    truck_polygonmesh_free(*mesh);
    return result;
}

Result<TessellatedGeometry> TruckShapeStorage::tessellate(const TessellationOptions& opts) const {
    auto polygon = solidToPolygon(solid_, tessellationTolerance(opts));
    if (!polygon)
        return std::unexpected(polygon.error());

    TruckPolygonBuffer buffer{};
    TruckError* err = nullptr;
    if (!truck_polygonmesh_to_buffer(*polygon, &buffer, &err)) {
        truck_polygonmesh_free(*polygon);
        return std::unexpected(Error::make(ErrorCode::Internal, errorMessage(err, "truck polygon buffer failed")));
    }

    TessellatedGeometry result;
    result.bounds = boundsFromMesh(*polygon);
    result.solidMesh = buildSurfaceMesh(buffer);
    if (opts.includeEdges)
        result.wireMesh = buildTriangleWireMesh(result.solidMesh);

    truck_polygonbuffer_free(buffer);
    truck_polygonmesh_free(*polygon);
    return result;
}

Shape makeTruckShape(TruckSolid* solid) {
    if (!solid)
        return Shape{};
    return makeShapeFromStorage(std::make_shared<TruckShapeStorage>(solid));
}

}  // namespace mulan::modeling
