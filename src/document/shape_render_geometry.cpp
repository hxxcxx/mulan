#include "shape_render_geometry.h"

#include <mulan/engine/vertex/vertex_buffer.h>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Poly_Triangulation.hxx>
#include <Bnd_Box.hxx>
#include <TopAbs.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>

namespace mulan::document {
namespace {

engine::AABB buildBounds(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return engine::AABB::empty();

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return engine::AABB::empty();

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    engine::AABB result;
    result.min = {xmin, ymin, zmin};
    result.max = {xmax, ymax, zmax};
    return result;
}

engine::Mesh buildFaceMesh(const TopoDS_Shape& shape, const engine::AABB& bounds) {
    if (shape.IsNull() || bounds.isEmpty()) return {};

    const double dx = bounds.max.x - bounds.min.x;
    const double dy = bounds.max.y - bounds.min.y;
    const double dz = bounds.max.z - bounds.min.z;
    const double maxDim = std::max({dx, dy, dz});
    const double deflection = maxDim * 0.001;

    TopoDS_Shape meshedShape = shape;
    BRepMesh_IncrementalMesh mesher(meshedShape, deflection, false, 0.5, true);
    mesher.Perform();

    // 两遍：先累计顶点/索引数，再一次性建 buffer 写入。
    uint32_t totalVerts = 0;
    uint32_t totalTris  = 0;
    for (TopExp_Explorer ex(meshedShape, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        if (!tri->HasNormals()) tri->ComputeNormals();
        totalVerts += static_cast<uint32_t>(tri->NbNodes());
        totalTris  += static_cast<uint32_t>(tri->NbTriangles());
    }
    if (totalVerts == 0) return {};

    engine::Mesh mesh;
    mesh.layout    = engine::layouts::surface();
    mesh.topology  = engine::PrimitiveTopology::TriangleList;
    mesh.indexType = engine::IndexType::UInt32;

    engine::VertexBufferBuilder vb(mesh.layout, totalVerts);
    mesh.indices.resize(static_cast<size_t>(totalTris) * 3 * sizeof(uint32_t));
    auto* idxOut = reinterpret_cast<uint32_t*>(mesh.indices.data());

    uint32_t baseVertex = 0;
    uint32_t idxSlot = 0;
    for (TopExp_Explorer ex(meshedShape, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;

        gp_Trsf trsf = loc.Transformation();
        const bool hasTransform = trsf.Form() != gp_Identity;

        const int nv = tri->NbNodes();
        const int nt = tri->NbTriangles();
        if (!tri->HasNormals()) tri->ComputeNormals();

        for (int i = 1; i <= nv; ++i) {
            gp_Pnt p = tri->Node(i);
            if (hasTransform) p.Transform(trsf);

            gp_Dir n(0, 0, 1);
            if (tri->HasNormals()) n = tri->Normal(i);
            if (hasTransform) n.Transform(trsf);

            const uint32_t v = baseVertex + static_cast<uint32_t>(i - 1);
            vb.setPosition(v, static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z()));
            vb.setNormal  (v, static_cast<float>(n.X()), static_cast<float>(n.Y()), static_cast<float>(n.Z()));
            // TexCoord0 是 Float2，面网格无 UV，补 0
            float uv[2] = {0.f, 0.f};
            vb.write(v, engine::VertexSemantic::TexCoord0, uv);
        }

        for (int i = 1; i <= nt; ++i) {
            int n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);
            idxOut[idxSlot++] = baseVertex + static_cast<uint32_t>(n1 - 1);
            idxOut[idxSlot++] = baseVertex + static_cast<uint32_t>(n2 - 1);
            idxOut[idxSlot++] = baseVertex + static_cast<uint32_t>(n3 - 1);
        }

        baseVertex += static_cast<uint32_t>(nv);
    }

    auto vertBytes = vb.data();
    mesh.vertices.assign(vertBytes.begin(), vertBytes.end());

    mesh.computeBounds();
    return mesh;
}

engine::Mesh buildEdgeMesh(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return {};

    // 先累计顶点数与索引数。
    uint32_t totalVerts = 0;
    uint32_t totalIdx   = 0;
    for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(ex.Current());
        BRepAdaptor_Curve curve(edge);
        try {
            GCPnts_TangentialDeflection discret(curve, curve.FirstParameter(),
                                                curve.LastParameter(), 0.1, 0.1, 2);
            const int np = discret.NbPoints();
            if (np < 2) continue;
            totalVerts += static_cast<uint32_t>(np);
            totalIdx   += static_cast<uint32_t>(np - 1) * 2;
        } catch (...) {
        }
    }
    if (totalVerts == 0) return {};

    engine::Mesh mesh;
    mesh.layout    = engine::layouts::surface();
    mesh.topology  = engine::PrimitiveTopology::LineList;
    mesh.indexType = engine::IndexType::UInt32;

    engine::VertexBufferBuilder vb(mesh.layout, totalVerts);
    mesh.indices.resize(static_cast<size_t>(totalIdx) * sizeof(uint32_t));
    auto* idxOut = reinterpret_cast<uint32_t*>(mesh.indices.data());

    uint32_t vi = 0;
    uint32_t idxSlot = 0;
    for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(ex.Current());
        BRepAdaptor_Curve curve(edge);

        try {
            GCPnts_TangentialDeflection discret(curve, curve.FirstParameter(),
                                                curve.LastParameter(), 0.1, 0.1, 2);
            if (discret.NbPoints() < 2) continue;

            for (int i = 1; i <= discret.NbPoints(); ++i) {
                gp_Pnt pt = discret.Value(i);
                vb.setPosition(vi, static_cast<float>(pt.X()), static_cast<float>(pt.Y()), static_cast<float>(pt.Z()));
                vb.setNormal  (vi, 0.f, 0.f, 0.f);
                float uv[2] = {0.f, 0.f};
                vb.write(vi, engine::VertexSemantic::TexCoord0, uv);

                if (i > 1) {
                    idxOut[idxSlot++] = vi - 1;
                    idxOut[idxSlot++] = vi;
                }
                ++vi;
            }
        } catch (...) {
        }
    }

    auto vertBytes = vb.data();
    mesh.vertices.assign(vertBytes.begin(), vertBytes.end());

    mesh.computeBounds();
    return mesh;
}

} // namespace

ShapeRenderGeometry buildShapeRenderGeometry(const TopoDS_Shape& shape) {
    ShapeRenderGeometry result;
    result.bounds = buildBounds(shape);
    result.faceMesh = buildFaceMesh(shape, result.bounds);
    result.edgeMesh = buildEdgeMesh(shape);
    return result;
}

} // namespace mulan::document
