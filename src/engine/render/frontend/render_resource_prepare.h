/**
 * @file render_resource_prepare.h
 * @brief 用于管理渲染资源准备阶段的资源列表，包括几何体、材质等。
 * @author hxxcxx
 * @date 2026-07-06
 */

#pragma once

#include "../asset_gpu_key.h"

#include <mulan/graphics/mesh.h>

#include <memory>
#include <span>
#include <vector>

namespace mulan::engine {

struct RenderGeometryPrepareDesc {
    AssetGpuKey resourceKey;
    /// 提交层拥有的不可变网格快照。渲染端可以跨线程读取，
    /// 不依赖 GeometryAsset 或 PreviewLayer 的生命周期。
    std::shared_ptr<const graphics::Mesh> mesh;
    bool forceUpdate = false;
};

class RenderResourcePrepareList {
public:
    void clear() { geometries_.clear(); }

    void addGeometry(AssetGpuKey resourceKey, const graphics::Mesh& mesh, bool forceUpdate = false) {
        if (!resourceKey || mesh.empty()) {
            return;
        }
        geometries_.push_back(RenderGeometryPrepareDesc{
                .resourceKey = resourceKey,
                .mesh = std::make_shared<graphics::Mesh>(mesh),
                .forceUpdate = forceUpdate,
        });
    }

    std::span<const RenderGeometryPrepareDesc> geometries() const { return geometries_; }

private:
    std::vector<RenderGeometryPrepareDesc> geometries_;
};

}  // namespace mulan::engine
