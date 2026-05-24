/**
 * @file Shape.h
 * @brief B-Rep 形状的内核无关抽象基类
 * @author hxxcxx
 * @date 2026-05-18
 *
 * Shape 是 Modeling 层的核心抽象。
 * 外部代码只持有 Shape，不感知底层几何内核（OCCT / 未来其他）。
 */
#pragma once

#include "ModelingExport.h"

#include <memory>
#include <string>

#include "mulan/Engine/Geometry/Mesh.h"
#include "mulan/Engine/Math/Math.h"

namespace mulan::modeling {

// ============================================================
// 内核标识
// ============================================================

enum class KernelId : uint8_t {
    OCCT = 0,
    // 未来扩展: Parasolid, OpenCascade, ...
};

// ============================================================
// 三角化参数（内核无关）
// ============================================================

struct TessellationParams {
    double linearDeflection = 0.001;   // 线性偏差（相对于包围盒最大尺寸的比率）
    double angularDeflection = 0.5;    // 角度偏差（弧度）
    bool   relative = true;            // 相对模式
};

// ============================================================
// Shape — B-Rep 形状抽象基类
// ============================================================

class MODELING_API Shape {
public:
    virtual ~Shape() = default;

    // --- 身份 ---
    virtual KernelId kernelId() const = 0;
    virtual const char* kernelName() const = 0;

    // --- 克隆 ---
    virtual std::unique_ptr<Shape> clone() const = 0;

    // --- 查询 ---
    virtual engine::AABB boundingBox() const = 0;
    virtual bool isNull() const = 0;
    virtual std::string dumpType() const = 0;

    // --- 变换 ---
    virtual void transform(const engine::Mat4& mat) = 0;

    // --- 三角化 ---
    virtual std::unique_ptr<engine::Mesh> triangulate(
        const TessellationParams& params = {}) const = 0;

    // --- 边线提取 ---
    virtual std::unique_ptr<engine::Mesh> extractEdges(
        const TessellationParams& params = {}) const = 0;

    // --- 布尔操作 ---
    virtual std::unique_ptr<Shape> booleanUnion(const Shape& tool) const = 0;
    virtual std::unique_ptr<Shape> booleanCut(const Shape& tool) const = 0;
    virtual std::unique_ptr<Shape> booleanIntersect(const Shape& tool) const = 0;
};

} // namespace mulan::Modeling
