/**
 * @file Geometry.h
 * @brief 几何基类 — 所有几何数据类型的抽象接口
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "DocumentExport.h"

#include "MulanGeo/Engine/Math/Math.h"
#include "MulanGeo/Engine/Math/AABB.h"

namespace MulanGeo::Engine {
class Mesh;
}

namespace MulanGeo::Document {

/// 几何体类型枚举
enum class GeometryType : uint8_t {
    Mesh,       // 三角网格（通用显示用）
    OCCTShape,  // OCCT B-Rep 形状
    // 未来可扩展: PointCloud, ParametricCurve, ...
};

/// 几何基类 — 持有具体几何数据，提供统一查询接口
class DOCUMENT_API Geometry {
public:
    virtual ~Geometry() = default;

    virtual GeometryType geometryType() const = 0;

    /// 类型名字符串，用于调试/显示
    virtual const char* typeName() const = 0;

    /// 获取用于显示的三角网格（可能延迟生成并缓存）
    /// @return nullptr 表示该几何体暂无可显示的网格
    virtual const Engine::Mesh* displayMesh() const = 0;

    /// 获取边线网格（用于 CAD 边线叠加渲染）
    /// 默认返回 nullptr，OCCT 形状等 B-Rep 几何可重写
    virtual const Engine::Mesh* edgeMesh() const { return nullptr; }

    /// 轴对齐包围盒
    virtual Engine::AABB boundingBox() const = 0;
};

} // namespace MulanGeo::Document
