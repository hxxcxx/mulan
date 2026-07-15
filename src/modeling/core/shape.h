/**
 * @file shape.h
 * @brief Shape —— 中立 B-Rep 形状句柄（值语义，PImpl）。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * Shape 是建模内核的公共门面：它持有一段 B-Rep 拓扑，但公共层完全不知道
 * 具体内核（OCCT/truck）。实际拓扑存放在 ShapeStorage 的派生类里（OCCT 后端
 * 为 OcctShapeStorage，内部是 TopoDS_Shape），通过虚函数分发。
 *
 * 因此：
 *   - modeling_core / asset / io / scene / view / app 源码中零内核类型
 *   - 内核头文件只允许出现在 modeling_occt / modeling_truck 的 .cpp
 *
 * Shape::tessellate() 是虚函数分发入口：调用方只看到 Shape，运行时多态到
 * 后端实现，调用方无需链接后端库（只要最终可执行文件链接了某个后端）。
 */
#pragma once

#include "modeling_core_export.h"
#include "tessellation.h"

#include <mulan/core/result/error.h>
#include <mulan/math/math.h>

#include <memory>

namespace mulan::modeling {

enum class BodyKind : uint8_t {
    Empty,
    Wire,
    Sheet,
    Solid,
    Compound,
};

/// B-Rep 拓扑的不透明存储基类。具体实现由后端（modeling_occt 等）派生。
/// 公共层只持有指向它的 shared_ptr，通过虚函数查询/采样。
class ShapeStorage {
public:
    virtual ~ShapeStorage() = default;

    virtual BodyKind bodyKind() const = 0;
    virtual math::AABB3 bounds() const = 0;
    /// 将 B-Rep 离散为显示网格。由后端实现（OCCT 后端用 BRepMesh/Poly/GCPnts）。
    virtual Result<TessellatedGeometry> tessellate(const TessellationOptions& opts) const = 0;
};

/// 值语义 B-Rep 形状句柄。可拷贝（共享底层拓扑）、可移动。
class MODELING_CORE_API Shape {
public:
    Shape();
    ~Shape();
    Shape(const Shape&);
    Shape& operator=(const Shape&);
    Shape(Shape&&) noexcept;
    Shape& operator=(Shape&&) noexcept;

    bool valid() const { return static_cast<bool>(storage_); }
    bool empty() const { return !valid(); }
    explicit operator bool() const { return valid(); }

    BodyKind bodyKind() const;
    math::AABB3 bounds() const;

    /// 离散为显示网格。内部经虚函数分发到具体后端实现。
    Result<TessellatedGeometry> tessellate(const TessellationOptions& opts = {}) const;

private:
    // 后端构造 Shape 时注入具体 ShapeStorage。
    friend Shape makeShapeFromStorage(std::shared_ptr<ShapeStorage> storage);
    /// 后端经此取 Shape 内部 storage(布尔第二操作数等)。仅后端 .cpp 使用。
    friend std::shared_ptr<ShapeStorage> storageOf(const Shape& s);
    explicit Shape(std::shared_ptr<ShapeStorage> storage) : storage_(std::move(storage)) {}

    std::shared_ptr<ShapeStorage> storage_;
};

/// 后端把具体 ShapeStorage 包成中立 Shape 的统一入口。
inline Shape makeShapeFromStorage(std::shared_ptr<ShapeStorage> storage) {
    return Shape(std::move(storage));
}

/// 后端取 Shape 内部 storage(返回 shared_ptr<ShapeStorage>,后端再 dynamic_cast 到具体类型)。
inline std::shared_ptr<ShapeStorage> storageOf(const Shape& s) {
    return s.storage_;
}

}  // namespace mulan::modeling
