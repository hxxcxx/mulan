/**
 * @file preview_layer.cpp
 * @brief 临时预览几何的分角色网格维护。
 * @author hxxcxx
 * @date 2026-07-07
 */

#include <mulan/view/core/preview_layer.h>

#include <mulan/asset/curve_mesh_builder.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace mulan::view {
namespace {

void advanceGeneration(uint64_t& generation) {
    ++generation;
    if (generation == 0) {
        generation = 1;
    }
}

bool sameMatrix(const math::Mat4& lhs, const math::Mat4& rhs) {
    for (size_t column = 0; column < 4; ++column) {
        if (lhs[column] != rhs[column]) {
            return false;
        }
    }
    return true;
}

bool sameReference(const PreviewReference& lhs, const PreviewReference& rhs) {
    return lhs.entity == rhs.entity && sameMatrix(lhs.worldTransform, rhs.worldTransform) &&
           lhs.overrideWorldTransform == rhs.overrideWorldTransform && lhs.role == rhs.role &&
           lhs.visible == rhs.visible;
}

bool referencesEqual(const std::vector<PreviewReference>& lhs, const std::vector<PreviewReference>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), sameReference);
}

}  // namespace

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
    rebuildRoleMeshes(PreviewVisualRole::Tool);
    touch();
}

void PreviewLayer::clearToolGeometry() {
    if (tool_curves_.empty() && tool_meshes_.empty()) {
        return;
    }

    tool_curves_.clear();
    tool_meshes_.clear();
    rebuildRoleMeshes(PreviewVisualRole::Tool);
    touch();
}

void PreviewLayer::setSnapGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    snap_curves_ = std::move(curves);
    snap_meshes_ = std::move(meshes);
    rebuildRoleMeshes(PreviewVisualRole::Snap);
    touch();
}

void PreviewLayer::clearSnapGeometry() {
    if (snap_curves_.empty() && snap_meshes_.empty()) {
        return;
    }

    snap_curves_.clear();
    snap_meshes_.clear();
    rebuildRoleMeshes(PreviewVisualRole::Snap);
    touch();
}

void PreviewLayer::setGripGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    grip_curves_ = std::move(curves);
    grip_meshes_ = std::move(meshes);
    rebuildRoleMeshes(PreviewVisualRole::Grip);
    touch();
}

void PreviewLayer::clearGripGeometry() {
    if (grip_curves_.empty() && grip_meshes_.empty()) {
        return;
    }

    grip_curves_.clear();
    grip_meshes_.clear();
    rebuildRoleMeshes(PreviewVisualRole::Grip);
    touch();
}

void PreviewLayer::setGripHotGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes) {
    grip_hot_curves_ = std::move(curves);
    grip_hot_meshes_ = std::move(meshes);
    rebuildRoleMeshes(PreviewVisualRole::GripHot);
    touch();
}

void PreviewLayer::clearGripHotGeometry() {
    if (grip_hot_curves_.empty() && grip_hot_meshes_.empty()) {
        return;
    }

    grip_hot_curves_.clear();
    grip_hot_meshes_.clear();
    rebuildRoleMeshes(PreviewVisualRole::GripHot);
    touch();
}

void PreviewLayer::setReferences(std::vector<PreviewReference> references) {
    if (referencesEqual(references_, references))
        return;
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
    for (const PreviewVisualRole role :
         { PreviewVisualRole::Tool, PreviewVisualRole::Snap, PreviewVisualRole::Grip, PreviewVisualRole::GripHot }) {
        rebuildRoleMeshes(role);
    }
    touch();
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

std::span<const PreviewDrawable> PreviewLayer::drawables(PreviewVisualRole role) const {
    const size_t index = previewVisualRoleIndex(role);
    if (role_drawable_counts_[index] == 0) {
        return {};
    }
    return std::span<const PreviewDrawable>(drawables_)
            .subspan(role_drawable_offsets_[index], role_drawable_counts_[index]);
}

void PreviewLayer::rebuildRoleMeshes(PreviewVisualRole role) {
    std::vector<PreviewDrawable> rebuilt;
    auto appendMesh = [&rebuilt, role](graphics::Mesh mesh) {
        if (mesh.empty()) {
            return;
        }
        rebuilt.push_back(PreviewDrawable{ std::move(mesh), role });
    };
    switch (role) {
    case PreviewVisualRole::Tool:
        if (!tool_curves_.empty()) {
            appendMesh(asset::buildCurveWireMesh(tool_curves_));
        }
        for (const graphics::Mesh& mesh : tool_meshes_) {
            appendMesh(mesh);
        }
        break;
    case PreviewVisualRole::Snap:
        if (!snap_curves_.empty()) {
            appendMesh(asset::buildCurveWireMesh(snap_curves_));
        }
        for (const graphics::Mesh& mesh : snap_meshes_) {
            appendMesh(mesh);
        }
        break;
    case PreviewVisualRole::Grip:
        if (!grip_curves_.empty()) {
            appendMesh(asset::buildCurveWireMesh(grip_curves_));
        }
        for (const graphics::Mesh& mesh : grip_meshes_) {
            appendMesh(mesh);
        }
        break;
    case PreviewVisualRole::GripHot:
        if (!grip_hot_curves_.empty()) {
            appendMesh(asset::buildCurveWireMesh(grip_hot_curves_));
        }
        for (const graphics::Mesh& mesh : grip_hot_meshes_) {
            appendMesh(mesh);
        }
        break;
    }

    const size_t roleIndex = previewVisualRoleIndex(role);
    const size_t oldOffset = role_drawable_offsets_[roleIndex];
    const size_t oldCount = role_drawable_counts_[roleIndex];
    drawables_.erase(drawables_.begin() + static_cast<std::ptrdiff_t>(oldOffset),
                     drawables_.begin() + static_cast<std::ptrdiff_t>(oldOffset + oldCount));
    drawables_.insert(drawables_.begin() + static_cast<std::ptrdiff_t>(oldOffset),
                      std::make_move_iterator(rebuilt.begin()), std::make_move_iterator(rebuilt.end()));

    meshes_.erase(meshes_.begin() + static_cast<std::ptrdiff_t>(oldOffset),
                  meshes_.begin() + static_cast<std::ptrdiff_t>(oldOffset + oldCount));
    std::vector<graphics::Mesh> rebuiltMeshes;
    rebuiltMeshes.reserve(rebuilt.size());
    for (size_t index = 0; index < rebuilt.size(); ++index) {
        rebuiltMeshes.push_back(drawables_[oldOffset + index].mesh);
    }
    meshes_.insert(meshes_.begin() + static_cast<std::ptrdiff_t>(oldOffset),
                   std::make_move_iterator(rebuiltMeshes.begin()), std::make_move_iterator(rebuiltMeshes.end()));

    role_drawable_counts_[roleIndex] = rebuilt.size();
    size_t offset = 0;
    for (size_t index = 0; index < kPreviewVisualRoleCount; ++index) {
        role_drawable_offsets_[index] = offset;
        offset += role_drawable_counts_[index];
    }
}

void PreviewLayer::touch() {
    advanceGeneration(generation_);
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
