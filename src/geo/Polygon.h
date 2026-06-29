/**
 * @file Polygon.h
 * @brief 多边形 / 折线（2D / 3D）
 * @author hxxcxx
 * @date 2026-06-29
 *
 * Poly2 为 2D 简单多边形：支持面积、周长、点包含（射线法）。
 * Poly3 为 3D 折线/多边形顶点集合：周长、凸包接口预留。
 */
#pragma once

#include "Vec2.h"
#include "Vec3.h"
#include "Mat3.h"
#include "Mat4.h"
#include "Tolerance.h"

#include <vector>

namespace mulan::geo {

// ============================================================
// Poly2 — 2D 多边形
// ============================================================

struct Poly2 {
    std::vector<Vec2> vertices;

    Poly2() = default;
    explicit Poly2(std::vector<Vec2> v) : vertices(std::move(v)) {}
    Poly2(std::initializer_list<Vec2> init) : vertices(init) {}

    bool   empty() const { return vertices.empty(); }
    size_t size()  const { return vertices.size(); }

    /// 周长
    double perimeter() const {
        if (vertices.size() < 2) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t j = (i + 1) % vertices.size();
            sum += (vertices[j] - vertices[i]).length();
        }
        return sum;
    }

    /// 有向面积（顶点逆时针为正，顺时针为负）。要求多边形闭合。
    double signedArea() const {
        if (vertices.size() < 3) return 0.0;
        double s = 0.0;
        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t j = (i + 1) % vertices.size();
            s += cross(vertices[i], vertices[j]);
        }
        return s * 0.5;
    }
    double area() const { return std::abs(signedArea()); }

    /// 点是否在多边形内（射线法，含边界模糊处理）
    bool contains(const Vec2& p) const {
        if (vertices.size() < 3) return false;
        bool inside = false;
        size_t n = vertices.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec2& a = vertices[i];
            const Vec2& b = vertices[j];
            if (((a.y > p.y) != (b.y > p.y)) &&
                (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + 1e-15) + a.x)) {
                inside = !inside;
            }
        }
        return inside;
    }

    /// 经 2D 矩阵变换（每个顶点按点变换 w=1）
    Poly2 transformed(const Mat3& m) const {
        std::vector<Vec2> out;
        out.reserve(vertices.size());
        for (const Vec2& v : vertices) {
            Vec3 r = m * Vec3(v.x, v.y, 1.0);
            out.emplace_back(r.x, r.y);
        }
        return Poly2(std::move(out));
    }
};

// ============================================================
// Poly3 — 3D 折线 / 顶点环
// ============================================================

struct Poly3 {
    std::vector<Vec3> vertices;

    Poly3() = default;
    explicit Poly3(std::vector<Vec3> v) : vertices(std::move(v)) {}
    Poly3(std::initializer_list<Vec3> init) : vertices(init) {}

    bool   empty() const { return vertices.empty(); }
    size_t size()  const { return vertices.size(); }

    /// 周长（闭合环）
    double perimeter() const {
        if (vertices.size() < 2) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < vertices.size(); ++i) {
            size_t j = (i + 1) % vertices.size();
            sum += (vertices[j] - vertices[i]).length();
        }
        return sum;
    }

    /// 折线总长（不闭合）
    double polylineLength() const {
        if (vertices.size() < 2) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i + 1 < vertices.size(); ++i) {
            sum += (vertices[i + 1] - vertices[i]).length();
        }
        return sum;
    }

    /// 经矩阵变换（每个顶点按点变换）
    Poly3 transformed(const Mat4& m) const {
        std::vector<Vec3> out;
        out.reserve(vertices.size());
        for (const Vec3& v : vertices) {
            out.push_back(transformPoint(m, v));
        }
        return Poly3(std::move(out));
    }
};

} // namespace mulan::geo
