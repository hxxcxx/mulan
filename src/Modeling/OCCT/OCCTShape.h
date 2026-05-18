/**
 * @file OCCTShape.h
 * @brief OCCT 内核的 Shape 实现
 * @author hxxcxx
 * @date 2026-05-18
 *
 * 头文件不暴露任何 OCCT 类型。
 * TopoDS_Shape 通过 pimpl 模式完全隐藏在 .cpp 中。
 */
#pragma once

#include "../ShapeImpl.h"

namespace MulanGeo::Modeling {

class OCCTShape final : public ShapeImpl<OCCTShape> {
public:
    static constexpr KernelId s_kernelId = KernelId::OCCT;
    static constexpr const char* s_kernelName = "occt";

    /// 默认构造（空 shape）
    OCCTShape();

    ~OCCTShape() override;

    OCCTShape(const OCCTShape&) = delete;
    OCCTShape& operator=(const OCCTShape&) = delete;
    OCCTShape(OCCTShape&&) noexcept;
    OCCTShape& operator=(OCCTShape&&) noexcept;

    // --- 基本体创建（分发到 OCCT BRepPrimAPI）---
    static std::unique_ptr<OCCTShape> createBox(double dx, double dy, double dz);
    static std::unique_ptr<OCCTShape> createCylinder(double radius, double height);
    static std::unique_ptr<OCCTShape> createSphere(double radius);
    static std::unique_ptr<OCCTShape> createCone(double radius, double height);
    static std::unique_ptr<OCCTShape> createTorus(double majorRadius, double minorRadius);

    // --- Derived 接口实现 ---
    std::unique_ptr<OCCTShape>    doClone() const;
    Engine::AABB                  doBoundingBox() const;
    bool                          doIsNull() const;
    std::string                   doDumpType() const;
    void                          doTransform(const Engine::Mat4& mat);
    std::unique_ptr<Engine::Mesh> doTriangulate(const TessellationParams& params) const;
    std::unique_ptr<Engine::Mesh> doExtractEdges(const TessellationParams& params) const;
    std::unique_ptr<OCCTShape>    doBoolean(int op, const OCCTShape& tool) const;

    // --- 工厂方法（定义在 .cpp 中，接受 TopoDS_Shape）---

    /// 从 OCCT TopoDS_Shape 移动构造
    static std::unique_ptr<OCCTShape> fromTopoDSShape(/*TopoDS_Shape*/ void* occtShape);

    /// 获取底层 TopoDS_Shape 的原始指针（仅内部使用）
    void* nativeHandle() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace MulanGeo::Modeling
