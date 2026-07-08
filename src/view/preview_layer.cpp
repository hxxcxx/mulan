/**
 * @file preview_layer.cpp
 * @brief Transient preview geometry mesh rebuild.
 * @author hxxcxx
 * @date 2026-07-07
 */

#include "preview_layer.h"

#include <mulan/asset/curve_mesh_builder.h>

#include <utility>

namespace mulan::view {

void PreviewLayer::setCurves(std::vector<asset::CurvePrimitive> primitives) {
    curves_ = std::move(primitives);
    meshes_.clear();
    rebuildCurves();
}

void PreviewLayer::setCurve(asset::CurvePrimitive primitive) {
    curves_.clear();
    curves_.push_back(std::move(primitive));
    meshes_.clear();
    rebuildCurves();
}

void PreviewLayer::setMeshes(std::vector<graphics::Mesh> meshes) {
    curves_.clear();
    meshes_ = std::move(meshes);
    touch();
}

void PreviewLayer::setMesh(graphics::Mesh mesh) {
    std::vector<graphics::Mesh> meshes;
    meshes.push_back(std::move(mesh));
    setMeshes(std::move(meshes));
}

void PreviewLayer::setGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    curves_ = std::move(curves);
    meshes_ = std::move(meshes);
    rebuildCurves();
}

void PreviewLayer::clear() {
    if (curves_.empty() && meshes_.empty()) {
        return;
    }

    curves_.clear();
    meshes_.clear();
    touch();
}

bool PreviewLayer::empty() const {
    for (const graphics::Mesh& mesh : meshes_) {
        if (!mesh.empty()) {
            return false;
        }
    }
    return true;
}

const graphics::Mesh& PreviewLayer::mesh() const {
    static const graphics::Mesh emptyMesh;
    return meshes_.empty() ? emptyMesh : meshes_.front();
}

void PreviewLayer::rebuildCurves() {
    if (!curves_.empty()) {
        graphics::Mesh wireMesh = asset::buildCurveWireMesh(curves_);
        meshes_.insert(meshes_.begin(), std::move(wireMesh));
    }
    touch();
}

void PreviewLayer::touch() {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
}

void PreviewBuilder::addCurve(asset::CurvePrimitive primitive) {
    curves_.push_back(std::move(primitive));
}

void PreviewBuilder::addSegment(const math::Segment3& segment) {
    addCurve(asset::CurvePrimitive::segment(segment));
}

void PreviewBuilder::addPolyline(const math::Polyline3& polyline) {
    addCurve(asset::CurvePrimitive::polyline(polyline));
}

void PreviewBuilder::addCircle(const math::Circle3& circle) {
    addCurve(asset::CurvePrimitive::circle(circle));
}

void PreviewBuilder::addArc(const math::Arc3& arc) {
    addCurve(asset::CurvePrimitive::arc(arc));
}

void PreviewBuilder::addMesh(graphics::Mesh mesh) {
    meshes_.push_back(std::move(mesh));
}

std::vector<asset::CurvePrimitive> PreviewBuilder::takeCurves() {
    return std::move(curves_);
}

std::vector<graphics::Mesh> PreviewBuilder::takeMeshes() {
    return std::move(meshes_);
}

}  // namespace mulan::view
