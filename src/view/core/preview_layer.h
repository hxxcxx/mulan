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
#include <mulan/math/math.h>
#include <mulan/scene/entity_id.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mulan::view {

enum class PreviewVisualRole : uint8_t {
    Tool,
    Snap,
    Grip,
    GripHot,
};

inline constexpr size_t kPreviewVisualRoleCount = 4;

constexpr size_t previewVisualRoleIndex(PreviewVisualRole role) {
    switch (role) {
    case PreviewVisualRole::Tool: return 0;
    case PreviewVisualRole::Snap: return 1;
    case PreviewVisualRole::Grip: return 2;
    case PreviewVisualRole::GripHot: return 3;
    }
    return 0;
}

struct PreviewDrawable {
    graphics::Mesh mesh;
    PreviewVisualRole role = PreviewVisualRole::Tool;
};

struct PreviewReference {
    scene::EntityId entity = scene::EntityId::invalid();
    math::Mat4 worldTransform{ 1.0 };
    bool overrideWorldTransform = false;
    PreviewVisualRole role = PreviewVisualRole::Tool;
    bool visible = true;

    bool valid() const { return visible && static_cast<bool>(entity); }
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
    void setReferences(std::vector<PreviewReference> references);
    void clearReferences();
    void clear();

    bool empty() const;
    uint64_t generation() const { return generation_; }
    /// 角色内任意直接几何或引用变化时递增，供上层快速判断角色差量。
    uint64_t roleGeneration(PreviewVisualRole role) const { return role_generations_[previewVisualRoleIndex(role)]; }
    /// 仅直接曲线/网格变化时递增；引用变化不会污染 GPU 几何上传判定。
    uint64_t geometryGeneration(PreviewVisualRole role) const {
        return geometry_generations_[previewVisualRoleIndex(role)];
    }
    /// 仅引用集合变化时递增。
    uint64_t referenceGeneration(PreviewVisualRole role) const {
        return reference_generations_[previewVisualRoleIndex(role)];
    }
    const graphics::Mesh& mesh() const;
    const std::vector<graphics::Mesh>& meshes() const { return meshes_; }
    const std::vector<PreviewDrawable>& drawables() const { return drawables_; }
    std::span<const PreviewDrawable> drawables(PreviewVisualRole role) const;
    const std::vector<PreviewReference>& references() const { return references_; }

private:
    void rebuildRoleMeshes(PreviewVisualRole role);
    void touchRoleChanges(const std::array<bool, kPreviewVisualRoleCount>& geometryChanged,
                          const std::array<bool, kPreviewVisualRoleCount>& referencesChanged);
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
    std::vector<PreviewReference> references_;
    std::array<size_t, kPreviewVisualRoleCount> role_drawable_offsets_{};
    std::array<size_t, kPreviewVisualRoleCount> role_drawable_counts_{};
    std::array<uint64_t, kPreviewVisualRoleCount> role_generations_{ 1, 1, 1, 1 };
    std::array<uint64_t, kPreviewVisualRoleCount> geometry_generations_{ 1, 1, 1, 1 };
    std::array<uint64_t, kPreviewVisualRoleCount> reference_generations_{ 1, 1, 1, 1 };
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
