/**
 * @file OCCTShape.cpp
 * @brief OCCT 内核的 Shape 实现 — 三角化、边线提取、布尔操作
 * @author hxxcxx
 * @date 2026-05-18
 *
 * OCCT 头文件只在此文件中出现，外部完全隔离。
 */
#include "OCCTShape.h"

#include <TopoDS_Shape.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>
#include <TopAbs.hxx>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>

#include <gp_Trsf.hxx>
#include <BRepBuilderAPI_Transform.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>

#include <algorithm>

namespace MulanGeo::Modeling {

// ============================================================
// pimpl — 持有 TopoDS_Shape
// ============================================================

struct OCCTShape::Impl {
    TopoDS_Shape shape;
    explicit Impl(TopoDS_Shape s) : shape(std::move(s)) {}
};

// ============================================================
// 构造 / 析构 / 移动
// ============================================================

OCCTShape::OCCTShape()
    : m_impl(new Impl(TopoDS_Shape()))
{}

OCCTShape::~OCCTShape() {
    delete m_impl;
}

OCCTShape::OCCTShape(OCCTShape&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

OCCTShape& OCCTShape::operator=(OCCTShape&& other) noexcept {
    if (this != &other) {
        delete m_impl;
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

// ============================================================
// 工厂方法 — 从 TopoDS_Shape 创建
// ============================================================

std::unique_ptr<OCCTShape> OCCTShape::fromTopoDSShape(void* occtShape) {
    auto ptr = static_cast<TopoDS_Shape*>(occtShape);
    auto result = std::make_unique<OCCTShape>();
    result->m_impl->shape = std::move(*ptr);
    return result;
}

void* OCCTShape::nativeHandle() const {
    return m_impl ? &m_impl->shape : nullptr;
}

// ============================================================
// 查询
// ============================================================

std::unique_ptr<OCCTShape> OCCTShape::doClone() const {
    auto result = std::make_unique<OCCTShape>();
    // TopoDS_Shape 赋值是引用计数浅拷贝，对只读使用安全
    result->m_impl->shape = m_impl->shape;
    return result;
}

Engine::AABB OCCTShape::doBoundingBox() const {
    Bnd_Box box;
    BRepBndLib::Add(m_impl->shape, box);
    if (box.IsVoid()) return Engine::AABB::empty();

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    Engine::AABB result;
    result.min = {xmin, ymin, zmin};
    result.max = {xmax, ymax, zmax};
    return result;
}

bool OCCTShape::doIsNull() const {
    return !m_impl || m_impl->shape.IsNull();
}

std::string OCCTShape::doDumpType() const {
    if (!m_impl || m_impl->shape.IsNull()) return "null";
    switch (m_impl->shape.ShapeType()) {
    case TopAbs_COMPOUND:  return "compound";
    case TopAbs_COMPSOLID: return "compsolid";
    case TopAbs_SOLID:     return "solid";
    case TopAbs_SHELL:     return "shell";
    case TopAbs_FACE:      return "face";
    case TopAbs_WIRE:      return "wire";
    case TopAbs_EDGE:      return "edge";
    case TopAbs_VERTEX:    return "vertex";
    case TopAbs_SHAPE:     return "shape";
    default:               return "unknown";
    }
}

// ============================================================
// 变换
// ============================================================

void OCCTShape::doTransform(const Engine::Mat4& mat) {
    gp_Trsf trsf;
    const double* d = glm::value_ptr(mat);
    trsf.SetValues(
        d[0], d[4], d[8],  d[12],
        d[1], d[5], d[9],  d[13],
        d[2], d[6], d[10], d[14]
    );
    BRepBuilderAPI_Transform transform(m_impl->shape, trsf, true);
    m_impl->shape = transform.Shape();
}

// ============================================================
// 三角化
// ============================================================

std::unique_ptr<Engine::Mesh> OCCTShape::doTriangulate(const TessellationParams& params) const {
    Bnd_Box box;
    BRepBndLib::Add(m_impl->shape, box);
    if (box.IsVoid()) return nullptr;

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    double dx = xmax - xmin;
    double dy = ymax - ymin;
    double dz = zmax - zmin;
    double maxDim = std::max({dx, dy, dz});
    double linearDeflection = params.relative
        ? maxDim * params.linearDeflection
        : params.linearDeflection;

    TopoDS_Shape shapeToMesh = m_impl->shape;
    BRepMesh_IncrementalMesh mesher(shapeToMesh, linearDeflection,
                                     params.relative, params.angularDeflection, true);
    mesher.Perform();
    if (!mesher.IsDone()) return nullptr;

    auto mesh = std::make_unique<Engine::Mesh>();

    for (TopExp_Explorer faceExp(shapeToMesh, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const auto& face = TopoDS::Face(faceExp.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;

        const gp_Trsf& trsf = loc.Transformation();
        uint32_t baseIndex = static_cast<uint32_t>(mesh->vertexCount());

        int nbNodes = tri->NbNodes();
        for (int i = 1; i <= nbNodes; i++) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            gp_Dir n(0, 0, 1);
            if (tri->HasNormals()) {
                n = tri->Normal(i).Transformed(trsf);
            }
            mesh->vertices.push_back(static_cast<float>(p.X()));
            mesh->vertices.push_back(static_cast<float>(p.Y()));
            mesh->vertices.push_back(static_cast<float>(p.Z()));
            mesh->vertices.push_back(static_cast<float>(n.X()));
            mesh->vertices.push_back(static_cast<float>(n.Y()));
            mesh->vertices.push_back(static_cast<float>(n.Z()));
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(0.0f);
        }

        int nbTris = tri->NbTriangles();
        for (int i = 1; i <= nbTris; i++) {
            int n0, n1, n2;
            tri->Triangle(i).Get(n0, n1, n2);
            mesh->indices.push_back(baseIndex + static_cast<uint32_t>(n0 - 1));
            mesh->indices.push_back(baseIndex + static_cast<uint32_t>(n1 - 1));
            mesh->indices.push_back(baseIndex + static_cast<uint32_t>(n2 - 1));
        }
    }

    if (!mesh->empty()) {
        mesh->computeBounds();
    }

    return mesh;
}

// ============================================================
// 边线提取
// ============================================================

std::unique_ptr<Engine::Mesh> OCCTShape::doExtractEdges(const TessellationParams& params) const {
    Bnd_Box box;
    BRepBndLib::Add(m_impl->shape, box);
    if (box.IsVoid()) return nullptr;

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    double dx = xmax - xmin;
    double dy = ymax - ymin;
    double dz = zmax - zmin;
    double maxDim = std::max({dx, dy, dz});
    double linearDeflection = params.relative
        ? maxDim * params.linearDeflection
        : params.linearDeflection;

    TopoDS_Shape shapeToMesh = m_impl->shape;
    BRepMesh_IncrementalMesh mesher(shapeToMesh, linearDeflection,
                                     params.relative, params.angularDeflection, true);
    mesher.Perform();
    if (!mesher.IsDone()) return nullptr;

    auto mesh = std::make_unique<Engine::Mesh>();
    mesh->topology = Engine::PrimitiveTopology::LineList;
    mesh->vertexStride = sizeof(float) * 8;

    for (TopExp_Explorer edgeExp(shapeToMesh, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
        const auto& edge = TopoDS::Edge(edgeExp.Current());

        TopLoc_Location loc;
        Handle(Poly_PolygonOnTriangulation) polyOnTri;
        Handle(Poly_Triangulation) tri;

        BRep_Tool::PolygonOnTriangulation(edge, polyOnTri, tri, loc, 1);
        if (polyOnTri.IsNull() || tri.IsNull()) continue;

        const TColStd_Array1OfInteger& indices = polyOnTri->Nodes();
        const gp_Trsf& trsf = loc.Transformation();

        int nbNodes = indices.Length();
        for (int i = indices.Lower(); i + 1 <= indices.Upper(); ++i) {
            int idx0 = indices(i);
            int idx1 = indices(i + 1);
            gp_Pnt p0 = tri->Node(idx0).Transformed(trsf);
            gp_Pnt p1 = tri->Node(idx1).Transformed(trsf);

            uint32_t baseIdx = static_cast<uint32_t>(mesh->vertexCount());

            mesh->vertices.push_back(static_cast<float>(p0.X()));
            mesh->vertices.push_back(static_cast<float>(p0.Y()));
            mesh->vertices.push_back(static_cast<float>(p0.Z()));
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(1.0f);
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(0.0f);

            mesh->vertices.push_back(static_cast<float>(p1.X()));
            mesh->vertices.push_back(static_cast<float>(p1.Y()));
            mesh->vertices.push_back(static_cast<float>(p1.Z()));
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(1.0f);
            mesh->vertices.push_back(0.0f);
            mesh->vertices.push_back(0.0f);

            mesh->indices.push_back(baseIdx);
            mesh->indices.push_back(baseIdx + 1);
        }
    }

    if (!mesh->empty()) {
        mesh->computeBounds();
    }

    return mesh;
}

// ============================================================
// 布尔操作
// ============================================================

std::unique_ptr<OCCTShape> OCCTShape::doBoolean(int op, const OCCTShape& tool) const {
    TopoDS_Shape result;

    switch (op) {
    case 0: {  // Union
        BRepAlgoAPI_Fuse fuse(m_impl->shape, tool.m_impl->shape);
        if (!fuse.IsDone()) return nullptr;
        result = fuse.Shape();
        break;
    }
    case 1: {  // Cut
        BRepAlgoAPI_Cut cut(m_impl->shape, tool.m_impl->shape);
        if (!cut.IsDone()) return nullptr;
        result = cut.Shape();
        break;
    }
    case 2: {  // Intersect
        BRepAlgoAPI_Common common(m_impl->shape, tool.m_impl->shape);
        if (!common.IsDone()) return nullptr;
        result = common.Shape();
        break;
    }
    default:
        return nullptr;
    }

    auto out = std::make_unique<OCCTShape>();
    out->m_impl->shape = std::move(result);
    return out;
}

// ============================================================
// 基本体创建
// ============================================================

std::unique_ptr<OCCTShape> OCCTShape::createBox(double dx, double dy, double dz) {
    BRepPrimAPI_MakeBox maker(dx, dy, dz);
    auto result = std::make_unique<OCCTShape>();
    result->m_impl->shape = maker.Shape();
    return result;
}

std::unique_ptr<OCCTShape> OCCTShape::createCylinder(double radius, double height) {
    BRepPrimAPI_MakeCylinder maker(radius, height);
    auto result = std::make_unique<OCCTShape>();
    result->m_impl->shape = maker.Shape();
    return result;
}

std::unique_ptr<OCCTShape> OCCTShape::createSphere(double radius) {
    BRepPrimAPI_MakeSphere maker(radius);
    auto result = std::make_unique<OCCTShape>();
    result->m_impl->shape = maker.Shape();
    return result;
}

std::unique_ptr<OCCTShape> OCCTShape::createCone(double radius, double height) {
    BRepPrimAPI_MakeCone maker(radius, 0.0, height);
    auto result = std::make_unique<OCCTShape>();
    result->m_impl->shape = maker.Shape();
    return result;
}

std::unique_ptr<OCCTShape> OCCTShape::createTorus(double majorRadius, double minorRadius) {
    BRepPrimAPI_MakeTorus maker(majorRadius, minorRadius);
    auto result = std::make_unique<OCCTShape>();
    result->m_impl->shape = maker.Shape();
    return result;
}

} // namespace MulanGeo::Modeling
