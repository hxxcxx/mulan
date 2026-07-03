#include "draw_command_builder.h"
#include "render_material_resolver.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/engine/render/render_resource_cache.h>
#include <mulan/engine/render/material/material_cache.h>
#include <mulan/render_scene/render_scene.h>
#include <mulan/render_scene/scene_proxy.h>

namespace mulan::view {
namespace {

/// 一个资产内多段可绘制网格的几何缓存 key：资产 id ^ (段序号 << 32)。
/// TessellatedAsset 通常 2 段（face/edge），Mesh 多段（每 primitive 一段）。
uint64_t drawableGeometryKey(asset::AssetId geometry, size_t drawableIndex) {
    return geometry.value ^ ((static_cast<uint64_t>(drawableIndex) + 1u) << 32u);
}

} // namespace

void DrawCommandBuilder::setScene(const render_scene::RenderScene* scene,
                                  const asset::AssetLibrary* assets) {
    scene_ = scene;
    assets_ = assets;
}

void DrawCommandBuilder::rebuild(engine::RenderResourceCache& resources,
                                 engine::PipelineState* solidPso,
                                 engine::PipelineState* wirePso) {
    clear();

    if (!scene_ || !assets_)
        return;

    auto& matCache = engine::MaterialCache::instance();
    RenderMaterialResolver materialResolver(*assets_);
    uint32_t nextObjectOffset = 0;

    // 复用临时容器，避免每个 proxy 重复分配。
    std::vector<asset::Drawable> drawables;

    scene_->forEachProxy([&](const render_scene::SceneProxy& proxy) {
        if (!proxy.visible || !proxy.geometry)
            return;

        const auto* baseAsset = assets_->asset(proxy.geometry);
        const auto* geom = dynamic_cast<const asset::GeometryAsset*>(baseAsset);
        if (!geom) return;

        drawables.clear();
        geom->collectDrawables(drawables);

        for (size_t i = 0; i < drawables.size(); ++i) {
            const auto& d = drawables[i];
            if (!d.mesh || d.mesh->empty()) continue;

            const uint64_t key = drawableGeometryKey(proxy.geometry, i);
            const bool isWire = (d.role == asset::DrawableRole::Wire);
            engine::PipelineState* pso = isWire ? wirePso : solidPso;

            // 上传/查询几何缓存（solid 与 wire 分桶）
            if (isWire) {
                if (!resources.wireGeometry(key))
                    resources.uploadWireGeometry(key, *d.mesh);
            } else {
                if (!resources.solidGeometry(key))
                    resources.uploadSolidGeometry(key, *d.mesh);
            }

            const auto* geo = isWire ? resources.wireGeometry(key)
                                     : resources.solidGeometry(key);
            if (!geo) continue;

            engine::MeshDrawCommand cmd;
            cmd.pipelineState   = pso;
            cmd.vertexBuffer    = geo->vertexBuffer.get();
            cmd.indexBuffer     = geo->indexBuffer.get();
            cmd.indexCount      = geo->indexCount;
            cmd.instanceCount   = 1;
            cmd.topology        = d.mesh->topology;
            cmd.objectUboOffset = nextObjectOffset;
            cmd.materialUboOffset = isWire
                ? materialResolver.materialOffset(asset::AssetId::invalid(), matCache)
                : materialResolver.materialOffset(d.material, matCache);
            cmd.worldTransform  = proxy.worldTransform;
            cmd.pickId          = proxy.entity.index();
            cmd.selected        = proxy.selected;
            cmd.isWire          = isWire;

            if (isWire)
                wire_cmds_.push_back(std::move(cmd));
            else
                solid_cmds_.push_back(std::move(cmd));
            nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
        }
    });
}

void DrawCommandBuilder::clear() {
    solid_cmds_.clear();
    wire_cmds_.clear();
}

} // namespace mulan::view
