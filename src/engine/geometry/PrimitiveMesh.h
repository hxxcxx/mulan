/**
 * @file PrimitiveMesh.h
 * @brief 参数化网格生成函数 — 纯数学，不依赖 OCCT
 * @author hxxcxx
 * @date 2026-05-19
 *
 * 所有函数返回完整的 engine::Mesh（pos+normal+texCoord 交错布局）。
 * 这是参数化 Geometry 的底层支撑：Entity 存参数 → 调这里生成 Mesh → 渲染。
 */
#pragma once

#include "../math/Math.h"
#include "../math/AABB.h"
#include "Mesh.h"

#include <cmath>
#include <memory>

namespace mulan::engine::primitive_mesh {

// ============================================================
// 常用数学常量
// ============================================================

static constexpr double kPi  = 3.14159265358979323846;
static constexpr double kPi2 = 2.0 * kPi;
static constexpr float  kPif = static_cast<float>(kPi);

// ============================================================
// 内部辅助：添加一个顶点 (pos3 + normal3 + texCoord2)
// ============================================================

inline void pushVertex(std::vector<float>& verts,
                       float px, float py, float pz,
                       float nx, float ny, float nz,
                       float u, float v)
{
    verts.push_back(px); verts.push_back(py); verts.push_back(pz);
    verts.push_back(nx); verts.push_back(ny); verts.push_back(nz);
    verts.push_back(u);  verts.push_back(v);
}

inline void pushTriangle(std::vector<uint32_t>& idx,
                         uint32_t a, uint32_t b, uint32_t c)
{
    idx.push_back(a); idx.push_back(b); idx.push_back(c);
}

// ============================================================
// 长方体 — 6 面 × 4 顶点 × 2 三角形 = 24 顶点, 36 索引
// ============================================================

inline std::unique_ptr<Mesh> box(double dx, double dy, double dz,
                                 const std::string& name = "Box")
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;

    double hx = dx * 0.5, hy = dy * 0.5, hz = dz * 0.5;

    // 每面 4 个顶点，6 面 = 24 个顶点
    auto& v = mesh->vertices;
    auto& i = mesh->indices;

    // Front (z+)
    pushVertex(v, -hx, -hy,  hz,  0, 0, 1,  0, 0);
    pushVertex(v,  hx, -hy,  hz,  0, 0, 1,  1, 0);
    pushVertex(v,  hx,  hy,  hz,  0, 0, 1,  1, 1);
    pushVertex(v, -hx,  hy,  hz,  0, 0, 1,  0, 1);
    // Back (z-)
    pushVertex(v,  hx, -hy, -hz,  0, 0,-1,  0, 0);
    pushVertex(v, -hx, -hy, -hz,  0, 0,-1,  1, 0);
    pushVertex(v, -hx,  hy, -hz,  0, 0,-1,  1, 1);
    pushVertex(v,  hx,  hy, -hz,  0, 0,-1,  0, 1);
    // Right (x+)
    pushVertex(v,  hx, -hy,  hz,  1, 0, 0,  0, 0);
    pushVertex(v,  hx, -hy, -hz,  1, 0, 0,  1, 0);
    pushVertex(v,  hx,  hy, -hz,  1, 0, 0,  1, 1);
    pushVertex(v,  hx,  hy,  hz,  1, 0, 0,  0, 1);
    // Left (x-)
    pushVertex(v, -hx, -hy, -hz, -1, 0, 0,  0, 0);
    pushVertex(v, -hx, -hy,  hz, -1, 0, 0,  1, 0);
    pushVertex(v, -hx,  hy,  hz, -1, 0, 0,  1, 1);
    pushVertex(v, -hx,  hy, -hz, -1, 0, 0,  0, 1);
    // Top (y+)
    pushVertex(v, -hx,  hy,  hz,  0, 1, 0,  0, 0);
    pushVertex(v,  hx,  hy,  hz,  0, 1, 0,  1, 0);
    pushVertex(v,  hx,  hy, -hz,  0, 1, 0,  1, 1);
    pushVertex(v, -hx,  hy, -hz,  0, 1, 0,  0, 1);
    // Bottom (y-)
    pushVertex(v, -hx, -hy, -hz,  0,-1, 0,  0, 0);
    pushVertex(v,  hx, -hy, -hz,  0,-1, 0,  1, 0);
    pushVertex(v,  hx, -hy,  hz,  0,-1, 0,  1, 1);
    pushVertex(v, -hx, -hy,  hz,  0,-1, 0,  0, 1);

    // 每面 2 个三角形
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        pushTriangle(i, base, base + 1, base + 2);
        pushTriangle(i, base, base + 2, base + 3);
    }

    mesh->computeBounds();
    return mesh;
}

// ============================================================
// 圆柱体 — 侧面 + 顶盖 + 底盖
// ============================================================

inline std::unique_ptr<Mesh> cylinder(double radius, double height,
                                      int segments = 32,
                                      const std::string& name = "Cylinder")
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;

    float r = static_cast<float>(radius);
    float h = static_cast<float>(height) * 0.5f;

    auto& v = mesh->vertices;
    auto& i = mesh->indices;
    uint32_t vertIdx = 0;

    // 侧面
    for (int seg = 0; seg <= segments; ++seg) {
        float theta = kPif * 2.0f * seg / segments;
        float c = std::cos(theta), s = std::sin(theta);
        // 底部顶点
        pushVertex(v, r * c, -h, r * s, c, 0, s,
                   static_cast<float>(seg) / segments, 0.0f);
        // 顶部顶点
        pushVertex(v, r * c,  h, r * s, c, 0, s,
                   static_cast<float>(seg) / segments, 1.0f);
    }
    for (int seg = 0; seg < segments; ++seg) {
        uint32_t bl = seg * 2, tl = seg * 2 + 1;
        uint32_t br = bl + 2, tr = tl + 2;
        pushTriangle(i, bl, br, tl);
        pushTriangle(i, tl, br, tr);
    }
    vertIdx = static_cast<uint32_t>(v.size() / 8);

    // 顶盖
    uint32_t topCenter = vertIdx;
    pushVertex(v, 0, h, 0, 0, 1, 0, 0.5f, 0.5f);
    for (int seg = 0; seg <= segments; ++seg) {
        float theta = kPif * 2.0f * seg / segments;
        pushVertex(v, r * std::cos(theta), h, r * std::sin(theta),
                   0, 1, 0, 0.5f + 0.5f * std::cos(theta), 0.5f + 0.5f * std::sin(theta));
    }
    for (int seg = 0; seg < segments; ++seg) {
        pushTriangle(i, topCenter, topCenter + 1 + seg, topCenter + 2 + seg);
    }
    vertIdx = static_cast<uint32_t>(v.size() / 8);

    // 底盖
    uint32_t botCenter = vertIdx;
    pushVertex(v, 0, -h, 0, 0, -1, 0, 0.5f, 0.5f);
    for (int seg = 0; seg <= segments; ++seg) {
        float theta = kPif * 2.0f * seg / segments;
        pushVertex(v, r * std::cos(theta), -h, r * std::sin(theta),
                   0, -1, 0, 0.5f + 0.5f * std::cos(theta), 0.5f - 0.5f * std::sin(theta));
    }
    for (int seg = 0; seg < segments; ++seg) {
        pushTriangle(i, botCenter, botCenter + 2 + seg, botCenter + 1 + seg);
    }

    mesh->computeBounds();
    return mesh;
}

// ============================================================
// 球体 — UV 球
// ============================================================

inline std::unique_ptr<Mesh> sphere(double radius,
                                    int rings = 16, int segments = 32,
                                    const std::string& name = "Sphere")
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;

    float r = static_cast<float>(radius);
    auto& v = mesh->vertices;
    auto& i = mesh->indices;

    for (int ring = 0; ring <= rings; ++ring) {
        float phi = kPif * ring / rings;
        float sinPhi = std::sin(phi), cosPhi = std::cos(phi);
        for (int seg = 0; seg <= segments; ++seg) {
            float theta = kPif * 2.0f * seg / segments;
            float sinT = std::sin(theta), cosT = std::cos(theta);
            float nx = sinPhi * cosT, ny = cosPhi, nz = sinPhi * sinT;
            pushVertex(v, r * nx, r * ny, r * nz,
                       nx, ny, nz,
                       static_cast<float>(seg) / segments,
                       static_cast<float>(ring) / rings);
        }
    }
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            uint32_t a = ring * (segments + 1) + seg;
            uint32_t b = a + segments + 1;
            pushTriangle(i, a, b, a + 1);
            pushTriangle(i, a + 1, b, b + 1);
        }
    }

    mesh->computeBounds();
    return mesh;
}

// ============================================================
// 圆锥体 — 底面在 y=-h/2，顶点在 y=+h/2
// ============================================================

inline std::unique_ptr<Mesh> cone(double radius, double height,
                                  int segments = 32,
                                  const std::string& name = "Cone")
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;

    float r = static_cast<float>(radius);
    float h = static_cast<float>(height);
    float halfH = h * 0.5f;

    auto& v = mesh->vertices;
    auto& i = mesh->indices;
    uint32_t vertIdx = 0;

    // 侧面
    for (int seg = 0; seg <= segments; ++seg) {
        float theta = kPif * 2.0f * seg / segments;
        float c = std::cos(theta), s = std::sin(theta);
        // 底部
        pushVertex(v, r * c, -halfH, r * s, c, r / h, s,
                   static_cast<float>(seg) / segments, 0.0f);
        // 顶部（锥顶）
        pushVertex(v, 0, halfH, 0, c, r / h, s,
                   static_cast<float>(seg) / segments, 1.0f);
    }
    for (int seg = 0; seg < segments; ++seg) {
        uint32_t bl = seg * 2, tl = seg * 2 + 1;
        uint32_t br = bl + 2, tr = tl + 2;
        pushTriangle(i, bl, br, tl);
        pushTriangle(i, tl, br, tr);
    }
    vertIdx = static_cast<uint32_t>(v.size() / 8);

    // 底盖
    uint32_t center = vertIdx;
    pushVertex(v, 0, -halfH, 0, 0, -1, 0, 0.5f, 0.5f);
    for (int seg = 0; seg <= segments; ++seg) {
        float theta = kPif * 2.0f * seg / segments;
        pushVertex(v, r * std::cos(theta), -halfH, r * std::sin(theta),
                   0, -1, 0, 0.5f + 0.5f * std::cos(theta), 0.5f - 0.5f * std::sin(theta));
    }
    for (int seg = 0; seg < segments; ++seg) {
        pushTriangle(i, center, center + 2 + seg, center + 1 + seg);
    }

    mesh->computeBounds();
    return mesh;
}

// ============================================================
// 圆环体 (Torus)
// ============================================================

inline std::unique_ptr<Mesh> torus(double majorRadius, double minorRadius,
                                   int majorSegments = 32, int minorSegments = 16,
                                   const std::string& name = "Torus")
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;

    float R = static_cast<float>(majorRadius);
    float r = static_cast<float>(minorRadius);
    auto& v = mesh->vertices;
    auto& idx = mesh->indices;

    for (int i = 0; i <= majorSegments; ++i) {
        float theta = kPif * 2.0f * i / majorSegments;
        float cosT = std::cos(theta), sinT = std::sin(theta);
        for (int j = 0; j <= minorSegments; ++j) {
            float phi = kPif * 2.0f * j / minorSegments;
            float cosP = std::cos(phi), sinP = std::sin(phi);
            float nx = cosP * cosT, ny = sinP, nz = cosP * sinT;
            pushVertex(v,
                       (R + r * cosP) * cosT, r * sinP, (R + r * cosP) * sinT,
                       nx, ny, nz,
                       static_cast<float>(i) / majorSegments,
                       static_cast<float>(j) / minorSegments);
        }
    }
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            uint32_t a = i * (minorSegments + 1) + j;
            uint32_t b = a + minorSegments + 1;
            pushTriangle(idx, a, b, a + 1);
            pushTriangle(idx, a + 1, b, b + 1);
        }
    }

    mesh->computeBounds();
    return mesh;
}

// ============================================================
// 线框矩形 (用于绘制预览)
// ============================================================

inline std::unique_ptr<Mesh> wireframeRect(const Vec3& p0, const Vec3& p1,
                                           const std::string& name = "PreviewRect")
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;
    mesh->topology = PrimitiveTopology::LineList;

    double minX = std::min(p0.x, p1.x), maxX = std::max(p0.x, p1.x);
    double minZ = std::min(p0.z, p1.z), maxZ = std::max(p0.z, p1.z);
    double y = p0.y;
    float f = static_cast<float>(y);

    auto& v = mesh->vertices;
    auto& idx = mesh->indices;

    // 4 corners (line list, no normals needed)
    float n = 0; // dummy normal
    pushVertex(v, (float)minX, f, (float)minZ, n,n,1, 0,0);
    pushVertex(v, (float)maxX, f, (float)minZ, n,n,1, 1,0);
    pushVertex(v, (float)maxX, f, (float)maxZ, n,n,1, 1,1);
    pushVertex(v, (float)minX, f, (float)maxZ, n,n,1, 0,1);

    // 4 edges = 8 indices (line list)
    pushTriangle(idx, 0, 1, 0); // workaround: use indices as pairs
    // Actually for LineList we need pairs
    idx.clear();
    auto addLine = [&](uint32_t a, uint32_t b) { idx.push_back(a); idx.push_back(b); };
    addLine(0, 1); addLine(1, 2); addLine(2, 3); addLine(3, 0);

    mesh->computeBounds();
    return mesh;
}

} // namespace mulan::engine::PrimitiveMesh
