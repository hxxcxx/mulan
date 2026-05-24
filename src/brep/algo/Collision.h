/**
 * @file Collision.h
 * @brief 三角形-三角形碰撞检测和射线穿面计数
 *
 * 基于 truck-meshalgo::analyzers::collision 和 in_out_judge。
 * 提供：
 *   - extractInterference: 三角网格之间的碰撞线段提取
 *   - signedCrossingFaces: 射线穿面计数（用于点在体内外判定）
 *   - pointInSolid: 射线法判定点是否在 Shell 内
 *
 * @author hxxcxx
 * @date 2026-05-23
 */
#pragma once

#include "../BRepExport.h"
#include "Triangulation.h"

#include <mulan/geometry/Types.h>
#include <mulan/geometry/Tolerance.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>
#include <utility>

namespace mulan::brep::tessellation {

using geometry::Point3;
using geometry::Vector3;
using geometry::near;
using geometry::soSmall;
using geometry::TOLERANCE;

// ============================================================
// 三角形-三角形相交检测
// ============================================================

struct Triangle {
    Point3 v[3];

    Point3& operator[](size_t i) { return v[i]; }
    const Point3& operator[](size_t i) const { return v[i]; }

    Vector3 normal() const {
        return glm::normalize(glm::cross(v[1] - v[0], v[2] - v[0]));
    }

    Vector3 unnormalizedNormal() const {
        return glm::cross(v[1] - v[0], v[2] - v[0]);
    }
};

struct LineSegment {
    Point3 p[2];

    Point3& operator[](size_t i) { return p[i]; }
    const Point3& operator[](size_t i) const { return p[i]; }
};

inline bool triangleTriangleIntersection(
    const Triangle& t0, const Triangle& t1,
    LineSegment& result)
{
    const Point3* a = t0.v;
    const Point3* b = t1.v;

    Vector3 n0 = t0.unnormalizedNormal();
    Vector3 n1 = t1.unnormalizedNormal();

    double d0 = glm::dot(n0, a[0]);
    double d1 = glm::dot(n1, b[0]);

    double dist_a0[3], dist_b0[3], dist_a1[3], dist_b1[3];
    for (int i = 0; i < 3; ++i) {
        dist_a1[i] = glm::dot(n0, b[i]) - d0;
        dist_b0[i] = glm::dot(n1, a[i]) - d1;
    }

    bool side_a0 = dist_a1[0] > 0;
    bool sb1_pos = dist_a1[1] > 0;
    bool sb2_pos = dist_a1[2] > 0;

    bool side_b0 = dist_b0[0] > 0;
    bool sa1_pos = dist_b0[1] > 0;
    bool sa2_pos = dist_b0[2] > 0;

    if (side_a0 == sb1_pos && side_a0 == sb2_pos)
        return false;
    if (side_b0 == sa1_pos && side_b0 == sa2_pos)
        return false;

    Vector3 dir = glm::cross(n0, n1);
    double len2 = glm::length2(dir);
    if (soSmall(len2))
        return false;

    double proj_a[3], proj_b[3];
    for (int i = 0; i < 3; ++i) {
        proj_a[i] = glm::dot(dir, a[i]);
        proj_b[i] = glm::dot(dir, b[i]);
    }

    double t_a0 = proj_a[0], t_a1 = proj_a[1], t_a2 = proj_a[2];
    double t_b0 = proj_b[0], t_b1 = proj_b[1], t_b2 = proj_b[2];

    auto solveEdge = [](double dp0, double dp1, double t0, double t1) -> double {
        return t0 + (t1 - t0) * dp0 / (dp0 - dp1);
    };

    double isect_a[2], isect_b[2];
    int ai = 0, bi = 0;

    if (side_a0 != sb1_pos)
        isect_a[ai++] = solveEdge(dist_a1[0], dist_a1[1], t_a0, t_a1);
    if (side_a0 != sb2_pos)
        isect_a[ai++] = solveEdge(dist_a1[0], dist_a1[2], t_a0, t_a2);
    if (ai == 0)
        isect_a[ai++] = solveEdge(dist_a1[1], dist_a1[2], t_a1, t_a2);

    if (side_b0 != sa1_pos)
        isect_b[bi++] = solveEdge(dist_b0[0], dist_b0[1], t_b0, t_b1);
    if (side_b0 != sa2_pos)
        isect_b[bi++] = solveEdge(dist_b0[0], dist_b0[2], t_b0, t_b2);
    if (bi == 0)
        isect_b[bi++] = solveEdge(dist_b0[1], dist_b0[2], t_b1, t_b2);

    if (ai < 2 || bi < 2)
        return false;

    double a_min = std::min(isect_a[0], isect_a[1]);
    double a_max = std::max(isect_a[0], isect_a[1]);
    double b_min = std::min(isect_b[0], isect_b[1]);
    double b_max = std::max(isect_b[0], isect_b[1]);

    if (a_max <= b_min + TOLERANCE || b_max <= a_min + TOLERANCE)
        return false;

    double t_lo = std::max(a_min, b_min);
    double t_hi = std::min(a_max, b_max);

    Point3 origin = a[0];
    double inv_len = 1.0 / std::sqrt(len2);
    Vector3 normalized_dir = dir * inv_len;

    result.p[0] = origin + normalized_dir * t_lo;
    result.p[1] = origin + normalized_dir * t_hi;
    return true;
}

inline std::vector<LineSegment> extractInterference(
    const TriMesh& mesh0, const TriMesh& mesh1)
{
    std::vector<LineSegment> segments;

    for (size_t i = 0; i < mesh0.triangleCount(); ++i) {
        Triangle t0;
        uint32_t i0 = mesh0.indices[i * 3 + 0];
        uint32_t i1 = mesh0.indices[i * 3 + 1];
        uint32_t i2 = mesh0.indices[i * 3 + 2];
        t0.v[0] = mesh0.positions[i0];
        t0.v[1] = mesh0.positions[i1];
        t0.v[2] = mesh0.positions[i2];

        Point3 center0 = (t0.v[0] + t0.v[1] + t0.v[2]) / 3.0;

        for (size_t j = 0; j < mesh1.triangleCount(); ++j) {
            Triangle t1;
            uint32_t j0 = mesh1.indices[j * 3 + 0];
            uint32_t j1 = mesh1.indices[j * 3 + 1];
            uint32_t j2 = mesh1.indices[j * 3 + 2];
            t1.v[0] = mesh1.positions[j0];
            t1.v[1] = mesh1.positions[j1];
            t1.v[2] = mesh1.positions[j2];

            double dist2 = glm::length2(
                (t1.v[0] + t1.v[1] + t1.v[2]) / 3.0 - center0);
            double max_dist2 = 0.0;
            for (int k = 0; k < 3; ++k) {
                for (int l = 0; l < 3; ++l) {
                    max_dist2 = std::max(max_dist2, glm::length2(t0.v[k] - t1.v[l]));
                }
            }
            if (dist2 > max_dist2 * 4.0)
                continue;

            LineSegment seg;
            if (triangleTriangleIntersection(t0, t1, seg)) {
                segments.push_back(seg);
            }
        }
    }

    return segments;
}

// ============================================================
// 射线穿面计数 — 点在体内外判定
// ============================================================

struct CrossingResult {
    int signed_count = 0;
    bool is_inside = false;
};

inline CrossingResult signedCrossingFaces(
    const TriMesh& mesh, const Point3& point, const Vector3& dir)
{
    int count = 0;

    for (size_t i = 0; i < mesh.triangleCount(); ++i) {
        uint32_t i0 = mesh.indices[i * 3 + 0];
        uint32_t i1 = mesh.indices[i * 3 + 1];
        uint32_t i2 = mesh.indices[i * 3 + 2];

        Point3 a = mesh.positions[i0];
        Point3 b = mesh.positions[i1];
        Point3 c = mesh.positions[i2];

        Vector3 e1 = b - a;
        Vector3 e2 = c - a;
        Vector3 h = glm::cross(dir, e2);
        double det = glm::dot(e1, h);

        if (std::abs(det) < TOLERANCE)
            continue;

        double inv_det = 1.0 / det;
        Vector3 s = point - a;
        double u = glm::dot(s, h) * inv_det;

        if (u < -TOLERANCE || u > 1.0 + TOLERANCE)
            continue;

        Vector3 q = glm::cross(s, e1);
        double v = glm::dot(dir, q) * inv_det;

        if (v < -TOLERANCE || u + v > 1.0 + TOLERANCE)
            continue;

        double t = glm::dot(e2, q) * inv_det;

        if (t > TOLERANCE) {
            Vector3 normal = mesh.normals.empty()
                ? glm::normalize(glm::cross(e1, e2))
                : (mesh.normals[i0] + mesh.normals[i1] + mesh.normals[i2]) / 3.0;

            double dot = glm::dot(normal, dir);
            if (dot > 0)
                count++;
            else
                count--;
        }
    }

    CrossingResult result;
    result.signed_count = count;
    result.is_inside = count > 0;
    return result;
}

inline Vector3 hashTakeOneUnit(const Point3& pt) {
    double x = pt.x, y = pt.y, z = pt.z;
    double ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
    double amax = std::max({ax, ay, az});

    if (amax < TOLERANCE) {
        return Vector3(1, 0, 0);
    }

    constexpr uint64_t HASH = 0x9e3779b97f4a7c15ULL;
    uint64_t bits;
    std::memcpy(&bits, &amax, 8);
    uint64_t hash_rot = (bits ^ HASH) & 0x3F;
    double angle = static_cast<double>(hash_rot) * 3.14159265358979323846 / 32.0;
    double ca = std::cos(angle), sa = std::sin(angle);

    Vector3 dir;
    if (ax >= ay && ax >= az) {
        dir = Vector3(x > 0 ? -1.0 : 1.0, ca, sa);
    } else if (ay >= ax && ay >= az) {
        dir = Vector3(sa, y > 0 ? -1.0 : 1.0, ca);
    } else {
        dir = Vector3(ca, sa, z > 0 ? -1.0 : 1.0);
    }
    return glm::normalize(dir);
}

inline bool pointInSolid(
    const TriMesh& shell_mesh, const Point3& point)
{
    Vector3 dir = hashTakeOneUnit(point);
    auto result = signedCrossingFaces(shell_mesh, point, dir);
    return result.is_inside;
}

} // namespace mulan::BRep::tessellation