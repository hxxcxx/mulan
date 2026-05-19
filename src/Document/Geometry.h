/**
 * @file Geometry.h
 * @brief 几何基类 — 所有几何数据类型的抽象接口
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 设计思路（借鉴老 BIM 系统经验）：
 *   - 参数化几何体只存参数（几个 double），不存三角网格
 *   - displayMesh() 按需从参数生成网格并缓存
 *   - 序列化只写参数，文件极小
 *   - 继承 Core::Object，获得多态序列化 + 工厂创建能力
 */
#pragma once

#include "DocumentExport.h"

#include "MulanGeo/Core/Reflection/Object.h"
#include "MulanGeo/Core/Reflection/Macro.h"

#include "MulanGeo/Engine/Math/Math.h"
#include "MulanGeo/Engine/Math/AABB.h"

namespace MulanGeo::Engine {
class Mesh;
}

namespace MulanGeo::Document {

/// 几何体类型枚举（序列化时写入文件，用于多态反序列化）
enum class GeometryType : uint8_t {
    // 参数化几何体 — 存参数，可重建，文件极小
    Box,
    Cylinder,
    Sphere,
    Cone,
    Torus,
    Extrusion,
    // 网格几何 — 存顶点/索引（导入的 STL/OBJ 等）
    Mesh,
    // OCCT B-Rep — 存 TopoDS_Shape 二进制（可选编译）
    OCCTShape,
    // 未来扩展
    // PointCloud,
    // ParametricCurve,
};

/// 几何基类 — 持有具体几何数据，提供统一查询接口
///
/// 继承 Core::Object 获得：
///   - serialize(OutputArchive&) / serialize(InputArchive&) 多态序列化
///   - ObjectFactory::create("BoxGeometry") 工厂创建
///   - MULANGEO_OBJECT 宏自动注册
class DOCUMENT_API Geometry : public Core::Object {
public:
    /// Geometry 是抽象基类，手动实现 classInfo/create，不使用 MULANGEO_OBJECT 宏
    static const Core::ClassInfo& staticClassInfo() {
        static const Core::ClassInfo s_info(
            "Geometry",
            Core::TypeInfo::of<Geometry>(),
            &Core::Object::staticClassInfo(),
            sizeof(Geometry),
            true);  // isAbstract = true
        return s_info;
    }

    const Core::ClassInfo& classInfo() const noexcept override {
        return staticClassInfo();
    }

    /// 抽象基类不可直接创建，但需要满足 Object 接口
    std::unique_ptr<Core::Object> create() const override {
        return nullptr;
    }

    virtual GeometryType geometryType() const = 0;

    /// 获取用于显示的三角网格（可能延迟生成并缓存）
    /// @return nullptr 表示该几何体暂无可显示的网格
    virtual const Engine::Mesh* displayMesh() const = 0;

    /// 获取边线网格（用于 CAD 边线叠加渲染）
    /// 默认返回 nullptr，B-Rep 类型可重写
    virtual const Engine::Mesh* edgeMesh() const { return nullptr; }

    /// 轴对齐包围盒
    virtual Engine::AABB boundingBox() const = 0;
};

} // namespace MulanGeo::Document
