/**
 * @file preview_layer.h
 * @brief 用于 view 渲染的临时可编辑预览几何。
 * @author hxxcxx
 * @date 2026-07-07
 *
 * PreviewLayer 不属于 Document、Scene 或 AssetLibrary。工具使用它承载绘制中的
 * 临时几何；这些几何需要参与渲染，但不能作为文档内容被选择、序列化、
 * 脏标记或撤销管理。
 */
#pragma once

#include <mulan/asset/curve_asset.h>
#include <mulan/graphics/mesh.h>

#include <cstdint>
#include <vector>

namespace mulan::view {

enum class PreviewVisualRole : uint8_t {
    Tool,
    Snap,
    Grip,
    GripHot,
};

struct PreviewDrawable {
    graphics::Mesh mesh;
    PreviewVisualRole role = PreviewVisualRole::Tool;
};

class PreviewLayer {
public:
    void setCurves(std::vector<asset::CurvePrimitive> primitives);
    void setCurve(asset::CurvePrimitive primitive);
    void setMeshes(std::vector<graphics::Mesh> meshes);
    void setMesh(graphics::Mesh mesh);
    void setGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes);
    void clearToolGeometry();
    void setSnapGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes);
    void clearSnapGeometry();
    void setGripGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes);
    void clearGripGeometry();
    void setGripHotGeometry(std::vector<asset::CurvePrimitive> curves, std::vector<graphics::Mesh> meshes);
    void clearGripHotGeometry();
    void clear();

    bool empty() const;
    uint64_t generation() const { return generation_; }
    const graphics::Mesh& mesh() const;
    const std::vector<graphics::Mesh>& meshes() const { return meshes_; }
    const std::vector<PreviewDrawable>& drawables() const { return drawables_; }

private:
    void rebuildMeshes();
    void touch();

    std::vector<asset::CurvePrimitive> tool_curves_;
    std::vector<graphics::Mesh> tool_meshes_;
    std::vector<asset::CurvePrimitive> snap_curves_;
    std::vector<graphics::Mesh> snap_meshes_;
    std::vector<asset::CurvePrimitive> grip_curves_;
    std::vector<graphics::Mesh> grip_meshes_;
    std::vector<asset::CurvePrimitive> grip_hot_curves_;
    std::vector<graphics::Mesh> grip_hot_meshes_;
    std::vector<graphics::Mesh> meshes_;
    std::vector<PreviewDrawable> drawables_;
    uint64_t generation_ = 1;
};

class PreviewBuilder {
public:
    void addCurve(asset::CurvePrimitive primitive);
    void addSegment(const math::Segment3& segment);
    void addPolyline(const math::Polyline3& polyline);
    void addCircle(const math::Circle3& circle);
    void addArc(const math::Arc3& arc);
    void addMesh(graphics::Mesh mesh);

    std::vector<asset::CurvePrimitive> takeCurves();
    std::vector<graphics::Mesh> takeMeshes();

private:
    std::vector<asset::CurvePrimitive> curves_;
    std::vector<graphics::Mesh> meshes_;
};

}  // namespace mulan::view
