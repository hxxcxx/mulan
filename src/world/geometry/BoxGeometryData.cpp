/**
 * @file BoxGeometryData.cpp
 * @brief 参数化长方体实现
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "BoxGeometryData.h"

namespace mulan::world {

BoxGeometryData::BoxGeometryData(double w, double h, double d)
    : m_width(w), m_height(h), m_depth(d) {
    ++m_dataVersion;
}

engine::Mesh BoxGeometryData::faceMesh() const {
    if (cacheValid(m_dataVersion, m_faceCacheVer))
        return m_cachedFaceMesh;

    m_cachedFaceMesh = generateFaceMesh();
    updateCacheVersion(m_faceCacheVer, m_dataVersion);
    return m_cachedFaceMesh;
}

engine::Mesh BoxGeometryData::edgeMesh() const {
    if (cacheValid(m_dataVersion, m_edgeCacheVer))
        return m_cachedEdgeMesh;

    m_cachedEdgeMesh = generateEdgeMesh();
    updateCacheVersion(m_edgeCacheVer, m_dataVersion);
    return m_cachedEdgeMesh;
}

engine::AABB BoxGeometryData::bounds() const {
    double hw = m_width  * 0.5;
    double hh = m_height * 0.5;
    double hd = m_depth  * 0.5;
    engine::AABB aabb;
    aabb.min = engine::Vec3{-hw, -hh, -hd};
    aabb.max = engine::Vec3{ hw,  hh,  hd};
    return aabb;
}

bool BoxGeometryData::setProperty(const std::string& name, double value) {
    if (name == "width")  { m_width  = value; ++m_dataVersion; return true; }
    if (name == "height") { m_height = value; ++m_dataVersion; return true; }
    if (name == "depth")  { m_depth  = value; ++m_dataVersion; return true; }
    return false;
}

// --- 顶点生成 ---

static constexpr int FLOAT_PER_VERT = 8;  // pos(3) + normal(3) + texCoord(2)

engine::Mesh BoxGeometryData::generateFaceMesh() const {
    double hw = m_width  * 0.5;
    double hh = m_height * 0.5;
    double hd = m_depth  * 0.5;

    // 8 corner positions
    engine::Vec3 p[8] = {
        {-hw, -hh, -hd}, { hw, -hh, -hd}, { hw,  hh, -hd}, {-hw,  hh, -hd},
        {-hw, -hh,  hd}, { hw, -hh,  hd}, { hw,  hh,  hd}, {-hw,  hh,  hd},
    };

    // 6 face normals
    engine::Vec3 n[6] = {
        { 0,  0, -1}, // front
        { 0,  0,  1}, // back
        { 0, -1,  0}, // bottom
        { 0,  1,  0}, // top
        {-1,  0,  0}, // left
        { 1,  0,  0}, // right
    };

    // Per-face: 4 vertices, 2 triangles
    // front (nz=-1): 0,1,2,3
    // back  (nz=+1): 5,4,7,6
    // bottom (ny=-1): 0,4,5,1
    // top   (ny=+1): 3,2,6,7
    // left  (nx=-1): 0,3,7,4
    // right (nx=+1): 1,5,6,2

    struct Face { int v[4]; int ni; };
    const Face faces[6] = {
        {{0,1,2,3}, 0}, {{5,4,7,6}, 1}, {{0,4,5,1}, 2},
        {{3,2,6,7}, 3}, {{0,3,7,4}, 4}, {{1,5,6,2}, 5},
    };

    engine::Mesh mesh;
    mesh.vertices.reserve(6 * 4 * FLOAT_PER_VERT);
    mesh.indices.reserve(6 * 6); // 6 faces × 2 triangles × 3 indices

    for (int f = 0; f < 6; ++f) {
        const auto& fc = faces[f];
        const auto& normal = n[fc.ni];
        uint32_t base = static_cast<uint32_t>(mesh.vertexCount());

        for (int v = 0; v < 4; ++v) {
            const auto& pos = p[fc.v[v]];
            mesh.vertices.insert(mesh.vertices.end(), {
                static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z),
                static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z),
                static_cast<float>(v & 1), static_cast<float>((v >> 1) & 1)});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base+1, base+2, base, base+2, base+3});
    }

    mesh.vertexStride = sizeof(float) * FLOAT_PER_VERT;
    mesh.topology = engine::PrimitiveTopology::TriangleList;
    mesh.computeBounds();
    return mesh;
}

engine::Mesh BoxGeometryData::generateEdgeMesh() const {
    double hw = m_width  * 0.5;
    double hh = m_height * 0.5;
    double hd = m_depth  * 0.5;

    // 8 corners
    engine::Vec3 p[8] = {
        {-hw, -hh, -hd}, { hw, -hh, -hd}, { hw,  hh, -hd}, {-hw,  hh, -hd},
        {-hw, -hh,  hd}, { hw, -hh,  hd}, { hw,  hh,  hd}, {-hw,  hh,  hd},
    };

    // 12 edges as (from, to) pairs
    struct Edge { int a, b; };
    static const Edge edges[12] = {
        {0,1},{1,2},{2,3},{3,0}, // front
        {4,5},{5,6},{6,7},{7,4}, // back
        {0,4},{1,5},{2,6},{3,7}, // connectors
    };

    engine::Mesh mesh;
    int verts = 12 * 2;
    mesh.vertices.reserve(verts * FLOAT_PER_VERT);
    mesh.indices.reserve(verts);

    for (const auto& e : edges) {
        const auto& a = p[e.a];
        const auto& b = p[e.b];
        mesh.vertices.insert(mesh.vertices.end(), {
            static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z),
            0.f, 0.f, 0.f, 0.f, 0.f,
            static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z),
            0.f, 0.f, 0.f, 0.f, 0.f});
        uint32_t base = static_cast<uint32_t>(mesh.vertexCount() - 2);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
    }

    mesh.vertexStride = sizeof(float) * FLOAT_PER_VERT;
    mesh.topology = engine::PrimitiveTopology::LineList;
    mesh.computeBounds();
    return mesh;
}

} // namespace mulan::world
