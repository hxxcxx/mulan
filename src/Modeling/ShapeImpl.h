/**
 * @file ShapeImpl.h
 * @brief CRTP 模板 — 消除 Shape 派生类的样板代码
 * @author hxxcxx
 * @date 2026-05-18
 *
 * Derived 必须实现：
 *   static constexpr KernelId s_kernelId;
 *   static constexpr const char* s_kernelName;
 *   std::unique_ptr<Derived>   doClone() const;
 *   engine::AABB               doBoundingBox() const;
 *   bool                       doIsNull() const;
 *   std::string                doDumpType() const;
 *   void                       doTransform(const engine::Mat4& mat);
 *   std::unique_ptr<engine::Mesh> doTriangulate(const TessellationParams&) const;
 *   std::unique_ptr<engine::Mesh> doExtractEdges(const TessellationParams&) const;
 *   std::unique_ptr<Derived>   doBoolean(int op, const Derived& tool) const;
 */
#pragma once

#include "Shape.h"

#include <cassert>

namespace MulanGeo::Modeling {

template<typename Derived>
class ShapeImpl : public Shape {
public:
    // --- 身份 ---
    KernelId kernelId() const override { return Derived::s_kernelId; }
    const char* kernelName() const override { return Derived::s_kernelName; }

    // --- 克隆 ---
    std::unique_ptr<Shape> clone() const override {
        return self().doClone();
    }

    // --- 查询 ---
    engine::AABB boundingBox() const override { return self().doBoundingBox(); }
    bool isNull() const override { return self().doIsNull(); }
    std::string dumpType() const override { return self().doDumpType(); }

    // --- 变换 ---
    void transform(const engine::Mat4& mat) override { selfMut().doTransform(mat); }

    // --- 三角化 ---
    std::unique_ptr<engine::Mesh> triangulate(
        const TessellationParams& params = {}) const override {
        return self().doTriangulate(params);
    }

    // --- 边线提取 ---
    std::unique_ptr<engine::Mesh> extractEdges(
        const TessellationParams& params = {}) const override {
        return self().doExtractEdges(params);
    }

    // --- 布尔操作 ---
    std::unique_ptr<Shape> booleanUnion(const Shape& tool) const override {
        return dispatchBoolean(0, tool);
    }

    std::unique_ptr<Shape> booleanCut(const Shape& tool) const override {
        return dispatchBoolean(1, tool);
    }

    std::unique_ptr<Shape> booleanIntersect(const Shape& tool) const override {
        return dispatchBoolean(2, tool);
    }

protected:
    /// 同内核布尔操作：安全 static_cast
    std::unique_ptr<Shape> doBooleanDispatch(int op, const Shape& tool) const {
        assert(tool.kernelId() == Derived::s_kernelId);
        return self().doBoolean(op, static_cast<const Derived&>(tool));
    }

private:
    const Derived& self() const { return static_cast<const Derived&>(*this); }
    Derived& selfMut() { return static_cast<Derived&>(*this); }

    std::unique_ptr<Shape> dispatchBoolean(int op, const Shape& tool) const {
        // 不同内核暂不支持混合布尔
        if (tool.kernelId() != Derived::s_kernelId) {
            return nullptr;
        }
        return doBooleanDispatch(op, tool);
    }
};

} // namespace MulanGeo::Modeling
