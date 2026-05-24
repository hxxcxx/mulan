/**
 * @file BoundingBox.h
 * @brief 轴对齐包围盒模板
 *
 * 基于 truck-base::bounding_box。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "Types.h"
#include "Export.h"
#include <limits>
#include <algorithm>

namespace MulanGeo::geometry {

/// 2D 轴对齐包围盒
struct GEOMETRY_API BoundingBox2D {
    Vector2 minPt;
    Vector2 maxPt;

    BoundingBox2D()
        : minPt(std::numeric_limits<double>::infinity())
        , maxPt(std::numeric_limits<double>::lowest()) {}

    void push(const Vector2& p) {
        minPt = glm::min(minPt, p);
        maxPt = glm::max(maxPt, p);
    }

    bool isEmpty() const {
        return minPt.x > maxPt.x || minPt.y > maxPt.y;
    }

    Vector2 diagonal() const { return maxPt - minPt; }
    Vector2 mid() const { return (minPt + maxPt) * 0.5; }
    double maxComponent() const {
        auto d = diagonal();
        return std::max({d.x, d.y});
    }

    bool intersects(const BoundingBox2D& other) const {
        return !(maxPt.x < other.minPt.x || other.maxPt.x < minPt.x ||
                 maxPt.y < other.minPt.y || other.maxPt.y < minPt.y);
    }

    void merge(const BoundingBox2D& other) {
        if (!other.isEmpty()) {
            push(other.minPt);
            push(other.maxPt);
        }
    }
};

/// 3D 轴对齐包围盒
struct GEOMETRY_API BoundingBox3D {
    Vector3 minPt;
    Vector3 maxPt;

    BoundingBox3D()
        : minPt(std::numeric_limits<double>::infinity())
        , maxPt(std::numeric_limits<double>::lowest()) {}

    void push(const Vector3& p) {
        minPt = glm::min(minPt, p);
        maxPt = glm::max(maxPt, p);
    }

    bool isEmpty() const {
        return minPt.x > maxPt.x || minPt.y > maxPt.y || minPt.z > maxPt.z;
    }

    Vector3 diagonal() const { return maxPt - minPt; }
    Vector3 mid() const { return (minPt + maxPt) * 0.5; }
    double maxComponent() const {
        auto d = diagonal();
        return std::max({d.x, d.y, d.z});
    }

    bool intersects(const BoundingBox3D& other) const {
        return !(maxPt.x < other.minPt.x || other.maxPt.x < minPt.x ||
                 maxPt.y < other.minPt.y || other.maxPt.y < minPt.y ||
                 maxPt.z < other.minPt.z || other.maxPt.z < minPt.z);
    }

    void merge(const BoundingBox3D& other) {
        if (!other.isEmpty()) {
            push(other.minPt);
            push(other.maxPt);
        }
    }
};

} // namespace MulanGeo::Geometry
