#include "solid_geometry_data.h"

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
#include <mutex>

namespace mulan::document {

struct SolidGeometryData::Impl {
    TopoDS_Shape shape;
    mutable engine::Mesh  cachedFaceMesh;
    mutable engine::Mesh  cachedEdgeMesh;
    mutable bool faceValid  = false;
    mutable bool edgeValid  = false;
    mutable std::mutex mtx;
};

SolidGeometryData::SolidGeometryData()
    : impl_(std::make_unique<Impl>()) {}

SolidGeometryData::~SolidGeometryData() = default;

SolidGeometryData::SolidGeometryData(const TopoDS_Shape& shape)
    : impl_(std::make_unique<Impl>()) {
    impl_->shape = shape;
}

void SolidGeometryData::setShape(const TopoDS_Shape& shape) {
    std::lock_guard lock(impl_->mtx);
    impl_->shape = shape;
    impl_->faceValid = false;
    impl_->edgeValid = false;
}

void SolidGeometryData::invalidateMeshCache() const {
    impl_->faceValid = false;
    impl_->edgeValid = false;
}

// ─── faceMesh ──────────────────────────────────────────────────

engine::Mesh SolidGeometryData::faceMesh() const {
    std::lock_guard lock(impl_->mtx);
    if (!impl_->faceValid) {
        impl_->cachedFaceMesh = triangulate();
        impl_->faceValid = true;
    }
    return impl_->cachedFaceMesh;
}

engine::Mesh SolidGeometryData::triangulate() const {
    auto& shape = impl_->shape;
    if (shape.IsNull()) return {};

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return {};

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    double maxDim = std::max({dx, dy, dz});
    double deflection = maxDim * 0.001;

    TopoDS_Shape s = shape;
    BRepMesh_IncrementalMesh mesher(s, deflection, false, 0.5, true);
    mesher.Perform();

    engine::Mesh mesh;
    mesh.vertexStride = sizeof(float) * 8; // pos3 + normal3 + tex2
    std::vector<float>&  verts = mesh.vertices;
    std::vector<uint32_t>& idx  = mesh.indices;

    uint32_t baseVertex = 0;
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        TopoDS_Face face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;

        gp_Trsf trsf = loc.Transformation();
        bool hasTransform = (trsf.Form() != gp_Identity);

        int nv = tri->NbNodes();
        int nt = tri->NbTriangles();

        // 确保法线已计算（OCCT 7.x：Poly_Triangulation 可缓存法线）
        bool hasNormals = tri->HasNormals();
        if (!hasNormals) tri->ComputeNormals();

        // vertices
        for (int i = 1; i <= nv; ++i) {
            gp_Pnt p = tri->Node(i);
            if (hasTransform) p.Transform(trsf);
            verts.insert(verts.end(), {
                static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())});

            // 法线：优先用 OCCT 解析法线（已包含曲面法向），并应用 face 变换
            gp_Dir n(0, 0, 1);
            if (tri->HasNormals()) {
                n = tri->Normal(i);
            }
            if (hasTransform) {
                n.Transform(trsf);
            }
            // OCCT 面法线可能朝外或朝内，与三角形绕序一致性由 shader 双面光照兜底
            verts.insert(verts.end(), {
                static_cast<float>(n.X()), static_cast<float>(n.Y()), static_cast<float>(n.Z())});

            // texcoords (unused)
            verts.insert(verts.end(), {0.f, 0.f});
        }

        // indices
        for (int i = 1; i <= nt; ++i) {
            int n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);
            idx.push_back(baseVertex + n1 - 1);
            idx.push_back(baseVertex + n2 - 1);
            idx.push_back(baseVertex + n3 - 1);
        }
        baseVertex += nv;
    }

    mesh.topology = engine::PrimitiveTopology::TriangleList;
    mesh.computeBounds();
    return mesh;
}

// ─── edgeMesh ──────────────────────────────────────────────────

engine::Mesh SolidGeometryData::edgeMesh() const {
    std::lock_guard lock(impl_->mtx);
    if (!impl_->edgeValid) {
        impl_->cachedEdgeMesh = extractEdges();
        impl_->edgeValid = true;
    }
    return impl_->cachedEdgeMesh;
}

engine::Mesh SolidGeometryData::extractEdges() const {
    auto& shape = impl_->shape;
    if (shape.IsNull()) return {};

    engine::Mesh mesh;
    mesh.vertexStride = sizeof(float) * 8;
    mesh.topology = engine::PrimitiveTopology::LineList;

    uint32_t vi = 0;
    for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(ex.Current());

        BRepAdaptor_Curve curve(edge);
        try {
            GCPnts_TangentialDeflection discret(curve, curve.FirstParameter(),
                                                  curve.LastParameter(), 0.1, 0.1, 2);
            if (discret.NbPoints() >= 2) {
                for (int i = 1; i <= discret.NbPoints(); ++i) {
                    gp_Pnt pt = discret.Value(i);
                    mesh.vertices.insert(mesh.vertices.end(), {
                        static_cast<float>(pt.X()), static_cast<float>(pt.Y()), static_cast<float>(pt.Z()),
                        0.f, 0.f, 0.f, 0.f, 0.f});
                    if (i > 1) {
                        mesh.indices.push_back(vi);
                        mesh.indices.push_back(vi + 1);
                    }
                    ++vi;
                }
            }
        } catch (...) {
            // skip problematic edges
        }
    }

    mesh.computeBounds();
    return mesh;
}

// ─── bounds ────────────────────────────────────────────────────

engine::AABB SolidGeometryData::bounds() const {
    auto& shape = impl_->shape;
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

} // namespace mulan::document
