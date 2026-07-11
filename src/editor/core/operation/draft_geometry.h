/**
 * @file draft_geometry.h
 * @brief 定义编辑过程中的临时几何。
 * @author hxxcxx
 * @date 2026-07-08
 */
#pragma once

#include <mulan/asset/curve_asset.h>
#include <mulan/graphics/mesh.h>

#include <utility>
#include <vector>

namespace mulan::editor {

class DraftGeometry {
public:
    static DraftGeometry curve(asset::CurvePrimitive primitive);
    static DraftGeometry curves(std::vector<asset::CurvePrimitive> primitives);
    static DraftGeometry segment(const math::Segment3& segment);
    static DraftGeometry mesh(graphics::Mesh mesh);
    static DraftGeometry meshes(std::vector<graphics::Mesh> meshes);
    static DraftGeometry geometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes);

    bool empty() const { return curves_.empty() && meshes_.empty(); }
    const std::vector<asset::CurvePrimitive>& curves() const { return curves_; }
    const std::vector<graphics::Mesh>& meshes() const { return meshes_; }
    std::vector<asset::CurvePrimitive> takeCurves() { return std::move(curves_); }
    std::vector<graphics::Mesh> takeMeshes() { return std::move(meshes_); }

private:
    DraftGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes)
        : curves_(std::move(curves)), meshes_(std::move(meshes)) {}

    std::vector<asset::CurvePrimitive> curves_;
    std::vector<graphics::Mesh> meshes_;
};

}  // namespace mulan::editor
