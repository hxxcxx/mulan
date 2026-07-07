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

class PreviewLayer {
public:
    void setCurves(std::vector<asset::CurvePrimitive> primitives);
    void setCurve(asset::CurvePrimitive primitive);
    void clear();

    bool empty() const { return mesh_.empty(); }
    uint64_t generation() const { return generation_; }
    const graphics::Mesh& mesh() const { return mesh_; }

private:
    void rebuild();
    void touch();

    std::vector<asset::CurvePrimitive> curves_;
    graphics::Mesh mesh_;
    uint64_t generation_ = 1;
};

class PreviewBuilder {
public:
    void addCurve(asset::CurvePrimitive primitive);
    void addSegment(const math::Segment3& segment);
    void addPolyline(const math::Polyline3& polyline);
    void addCircle(const math::Circle3& circle);
    void addArc(const math::Arc3& arc);

    std::vector<asset::CurvePrimitive> takeCurves();

private:
    std::vector<asset::CurvePrimitive> curves_;
};

}  // namespace mulan::view
