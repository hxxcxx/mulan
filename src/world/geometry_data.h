/**
 * @file geometry_data.h
 * @brief 几何数据接口 — 参数化几何 + mesh 缓存
 *
 * 设计原则：
 * - Entity 持有 unique_ptr<GeometryData>，不派生 Entity
 * - 新增几何类型只加 GeometryData 子类
 * - faceMesh() / edgeMesh() 惰性生成 + 版本缓存
 * - 数据修改通过 setProperty() → dataVersion++ → 下次 faceMesh() 重新三角化
 *
 * @author hxxcxx
 * @date 2026-05-29
 */

#pragma once

#include "mulan/engine/geometry/mesh.h"

#include <cstdint>
#include <optional>
#include <string>

namespace mulan::world {

class GeometryData {
public:
    enum class Type : uint8_t {
        Unknown = 0,
        Box,
        Cylinder,
        Sphere,
        Mesh,        // OBJ/STL/glTF pre-triangulated
        Solid,       // OCCT Shape
        Line,        // DXF line segment
        Arc,         // DXF arc
        Polyline,    // DXF polyline
        Text,
        PointCloud,
    };

    virtual ~GeometryData() = default;

    /// 几何类型（RenderSystem switch 用）
    virtual Type type() const = 0;

    /// 三角面 mesh（惰性缓存）
    virtual engine::Mesh faceMesh() const { return {}; }

    /// 边界线 mesh（惰性缓存）
    virtual engine::Mesh edgeMesh() const { return {}; }

    /// 局部空间的包围盒
    virtual engine::AABB bounds() const { return engine::AABB::empty(); }

    /// 修改参数化属性 → version++ → 缓存失效
    /// @return 是否支持该属性名
    virtual bool setProperty(const std::string& /*name*/, double /*value*/) { return false; }
    virtual bool setProperty(const std::string& /*name*/, const std::string& /*value*/) { return false; }

protected:
    /// 检查缓存是否有效
    bool cacheValid(uint64_t dataVersion, uint64_t cacheVersion) const {
        return cacheVersion == dataVersion && cacheVersion != 0;
    }

    /// 标记缓存版本
    void updateCacheVersion(uint64_t& cacheVersion, uint64_t dataVersion) const {
        cacheVersion = dataVersion;
    }
};

} // namespace mulan::world
