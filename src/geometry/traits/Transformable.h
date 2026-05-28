/**
 * @file Transformable.h
 * @brief 可变换/可翻转/几何等价转换特征接口
 *
 * 基于 truck-geotrait::traits 中的 Invertible, ToSameGeometry, IncludeCurve。
 * transformBy 已移至 BoundedCurve / ParametricSurface 基类。
 *
 * @author hxxcxx
 * @date 2026-05-20
 */
#pragma once

#include "../Types.h"
#include "../Export.h"

namespace mulan::geometry {

/// 可翻转接口
template<typename T>
class Invertible {
public:
    virtual ~Invertible() = default;
    virtual void invert() = 0;

    T inverse() const {
        T c = static_cast<const T&>(*this);
        c.invert();
        return c;
    }
};

/// 几何等价转换接口 (将 From 类型转换为 To 类型)
template<typename From, typename To>
class ToSameGeometry {
public:
    virtual ~ToSameGeometry() = default;
    virtual To toSameGeometry() const = 0;
};

/// 曲面包含曲线判断接口 (拓扑一致性检查需要)
template<typename C>
class IncludeCurve {
public:
    virtual ~IncludeCurve() = default;
    virtual bool include(const C& curve) const = 0;
};

} // namespace mulan::geometry
