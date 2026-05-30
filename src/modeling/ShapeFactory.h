/**
 * @file ShapeFactory.h
 * @brief 基本体创建的内核无关门面
 * @author hxxcxx
 * @date 2026-05-18
 *
 * ShapeFactory 根据 KernelId 分发到对应内核后端的基本体创建方法。
 * 外部代码只需调用 ShapeFactory::box(...) 即可，无需感知具体内核。
 *
 * 也可以直接调用特定内核的创建方法，如 OCCTShape::createBox(...)。
 */
#pragma once

#include "ModelingExport.h"
#include "Shape.h"

#include <memory>

namespace mulan::modeling {

class MODELING_API ShapeFactory {
public:
    /// 创建立方体/长方体
    /// @param kernel 使用的几何内核
    /// @param dx, dy, dz 三个方向的尺寸
    static std::unique_ptr<Shape> box(KernelId kernel, double dx, double dy, double dz);

    /// 创建圆柱体（Z 轴方向）
    static std::unique_ptr<Shape> cylinder(KernelId kernel, double radius, double height);

    /// 创建球体
    static std::unique_ptr<Shape> sphere(KernelId kernel, double radius);

    /// 创建圆锥体（Z 轴方向）
    static std::unique_ptr<Shape> cone(KernelId kernel, double radius, double height);

    /// 创建圆环体
    static std::unique_ptr<Shape> torus(KernelId kernel, double majorRadius, double minorRadius);
};

} // namespace mulan::modeling
