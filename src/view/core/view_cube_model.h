/**
 * @file view_cube_model.h
 * @brief ViewCubeModel 负责视图侧布局、命中测试和视角选择。
 * @author hxxcxx
 * @date 2026-07-20
 */
#pragma once

#include <mulan/render/overlay/view_cube_contract.h>

namespace mulan::view {

class ViewCubeModel {
public:
    explicit ViewCubeModel(engine::ViewCubeLayout layout = {}) : layout_(layout) {}

    const engine::ViewCubeLayout& layout() const { return layout_; }
    void setLayout(const engine::ViewCubeLayout& layout) { layout_ = layout; }
    engine::ViewCubeHit pickPart(int32_t screenX, int32_t screenY, uint32_t viewportWidth, uint32_t viewportHeight,
                                 const math::Mat4& mainViewMatrix) const;

    static math::Vec3 partNormal(const engine::ViewCubePart& part) {
        return engine::ViewCubeGeometry::partNormal(part);
    }

private:
    engine::ViewCubeHit hitTest(int32_t screenX, int32_t screenY, uint32_t viewportWidth,
                                uint32_t viewportHeight) const;
    engine::ViewCubeLayout layout_;
};

}  // namespace mulan::view
