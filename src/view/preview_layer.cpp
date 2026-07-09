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
    setGeometry(std::move(primitives), {});
}

void PreviewLayer::setCurve(asset::CurvePrimitive primitive) {
    std::vector<asset::CurvePrimitive> curves;
    curves.push_back(std::move(primitive));
    setCurves(std::move(curves));
}

void PreviewLayer::setMeshes(std::vector<graphics::Mesh> meshes) {
    setGeometry({}, std::move(meshes));
}

void PreviewLayer::setMesh(graphics::Mesh mesh) {
    std::vector<graphics::Mesh> meshes;
    meshes.push_back(std::move(mesh));
    setMeshes(std::move(meshes));
}

void PreviewLayer::setGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    tool_curves_ = std::move(curves);
    tool_meshes_ = std::move(meshes);
    rebuildMeshes();
}

void PreviewLayer::clearToolGeometry() {
    if (tool_curves_.empty() && tool_meshes_.empty()) {
        return;
    }

    tool_curves_.clear();
    tool_meshes_.clear();
    rebuildMeshes();
}

void PreviewLayer::setSnapGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    snap_curves_ = std::move(curves);
    snap_meshes_ = std::move(meshes);
    rebuildMeshes();
}

void PreviewLayer::clearSnapGeometry() {
    if (snap_curves_.empty() && snap_meshes_.empty()) {
        return;
    }

    snap_curves_.clear();
    snap_meshes_.clear();
    rebuildMeshes();
}

void PreviewLayer::setGripGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    grip_curves_ = std::move(curves);
    grip_meshes_ = std::move(meshes);
    rebuildMeshes();
}

void PreviewLayer::clearGripGeometry() {
    if (grip_curves_.empty() && grip_meshes_.empty()) {
        return;
    }

    grip_curves_.clear();
    grip_meshes_.clear();
    rebuildMeshes();
}

void PreviewLayer::setGripHotGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    grip_hot_curves_ = std::move(curves);
    grip_hot_meshes_ = std::move(meshes);
    rebuildMeshes();
}

void PreviewLayer::clearGripHotGeometry() {
    if (grip_hot_curves_.empty() && grip_hot_meshes_.empty()) {
        return;
    }

    grip_hot_curves_.clear();
    grip_hot_meshes_.clear();
    rebuildMeshes();
}

void PreviewLayer::setReferences(std::vector<PreviewReference> references) {
    references_ = std::move(references);
    touch();
}

void PreviewLayer::clearReferences() {
    if (references_.empty()) {
        return;
    }

    references_.clear();
    touch();
}

void PreviewLayer::clear() {
    if (tool_curves_.empty() && tool_meshes_.empty() && snap_curves_.empty() && snap_meshes_.empty() &&
        grip_curves_.empty() && grip_meshes_.empty() && grip_hot_curves_.empty() && grip_hot_meshes_.empty() &&
        references_.empty()) {
        return;
    }

    tool_curves_.clear();
    tool_meshes_.clear();
    snap_curves_.clear();
    snap_meshes_.clear();
    grip_curves_.clear();
    grip_meshes_.clear();
    grip_hot_curves_.clear();
    grip_hot_meshes_.clear();
    references_.clear();
    rebuildMeshes();
}

bool PreviewLayer::empty() const {
    for (const PreviewDrawable& drawable : drawables_) {
        if (!drawable.mesh.empty()) {
            return false;
        }
    }
    for (const PreviewReference& reference : references_) {
        if (reference.valid()) {
            return false;
        }
    }
    return true;
}

const graphics::Mesh& PreviewLayer::mesh() const {
    static const graphics::Mesh emptyMesh;
    return drawables_.empty() ? emptyMesh : drawables_.front().mesh;
}

void PreviewLayer::rebuildMeshes() {
    meshes_.clear();
    drawables_.clear();
    auto appendMesh = [this](graphics::Mesh mesh, PreviewVisualRole role) {
        if (mesh.empty()) {
            return;
        }
        drawables_.push_back(PreviewDrawable{ std::move(mesh), role });
        meshes_.push_back(drawables_.back().mesh);
    };

    if (!tool_curves_.empty()) {
        appendMesh(asset::buildCurveWireMesh(tool_curves_), PreviewVisualRole::Tool);
    }
    for (graphics::Mesh& mesh : tool_meshes_) {
        appendMesh(mesh, PreviewVisualRole::Tool);
    }
    if (!snap_curves_.empty()) {
        appendMesh(asset::buildCurveWireMesh(snap_curves_), PreviewVisualRole::Snap);
    }
    for (graphics::Mesh& mesh : snap_meshes_) {
        appendMesh(mesh, PreviewVisualRole::Snap);
    }
    if (!grip_curves_.empty()) {
        appendMesh(asset::buildCurveWireMesh(grip_curves_), PreviewVisualRole::Grip);
    }
    for (graphics::Mesh& mesh : grip_meshes_) {
        appendMesh(mesh, PreviewVisualRole::Grip);
    }
    if (!grip_hot_curves_.empty()) {
        appendMesh(asset::buildCurveWireMesh(grip_hot_curves_), PreviewVisualRole::GripHot);
    }
    for (graphics::Mesh& mesh : grip_hot_meshes_) {
        appendMesh(mesh, PreviewVisualRole::GripHot);
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
