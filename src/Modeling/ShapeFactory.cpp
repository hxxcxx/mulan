/**
 * @file ShapeFactory.cpp
 * @brief ShapeFactory 实现 — 根据 KernelId 分发到对应内核
 * @author hxxcxx
 * @date 2026-05-18
 */
#include "ShapeFactory.h"
#include "OCCT/OCCTShape.h"

namespace MulanGeo::Modeling {

std::unique_ptr<Shape> ShapeFactory::box(KernelId kernel, double dx, double dy, double dz) {
    switch (kernel) {
    case KernelId::OCCT:
        return OCCTShape::createBox(dx, dy, dz);
    }
    return nullptr;
}

std::unique_ptr<Shape> ShapeFactory::cylinder(KernelId kernel, double radius, double height) {
    switch (kernel) {
    case KernelId::OCCT:
        return OCCTShape::createCylinder(radius, height);
    }
    return nullptr;
}

std::unique_ptr<Shape> ShapeFactory::sphere(KernelId kernel, double radius) {
    switch (kernel) {
    case KernelId::OCCT:
        return OCCTShape::createSphere(radius);
    }
    return nullptr;
}

std::unique_ptr<Shape> ShapeFactory::cone(KernelId kernel, double radius, double height) {
    switch (kernel) {
    case KernelId::OCCT:
        return OCCTShape::createCone(radius, height);
    }
    return nullptr;
}

std::unique_ptr<Shape> ShapeFactory::torus(KernelId kernel, double majorRadius, double minorRadius) {
    switch (kernel) {
    case KernelId::OCCT:
        return OCCTShape::createTorus(majorRadius, minorRadius);
    }
    return nullptr;
}

} // namespace MulanGeo::Modeling
