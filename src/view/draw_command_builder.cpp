#include "draw_command_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/engine/render/render_resource_cache.h>
#include <mulan/engine/render/material/material_cache.h>
#include <mulan/render_scene/render_scene.h>
#include <mulan/render_scene/scene_proxy.h>

namespace mulan::view {

void DrawCommandBuilder::setScene(const render_scene::RenderScene* scene,
                                  const asset::AssetLibrary* assets) {
    scene_ = scene;
    assets_ = assets;
}

void DrawCommandBuilder::rebuild(engine::RenderResourceCache& gpu,
                                 engine::PipelineState* facePso,
                                 engine::PipelineState* edgePso) {
    clear();

    if (!scene_ || !assets_)
        return;

    auto& matCache = engine::MaterialCache::instance();
    uint32_t nextObjectOffset = 0;

    scene_->forEachProxy([&](const render_scene::SceneProxy& proxy) {
        if (!proxy.visible || !proxy.geometry)
            return;

        const auto* asset = assets_->asset(proxy.geometry);
        const engine::Mesh* faceMesh = nullptr;
        const engine::Mesh* edgeMesh = nullptr;

        if (const auto* brep = dynamic_cast<const asset::BRepAsset*>(asset)) {
            faceMesh = &brep->faceMesh();
            edgeMesh = &brep->edgeMesh();
        } else if (const auto* mesh = dynamic_cast<const asset::MeshAsset*>(asset)) {
            faceMesh = &mesh->mesh();
        }

        const uint64_t key = proxy.geometry.value;
        if (faceMesh && !faceMesh->empty()) {
            if (!gpu.faceGeometry(key))
                gpu.uploadFaceMesh(key, *faceMesh);

            if (const auto* geo = gpu.faceGeometry(key)) {
                engine::MeshDrawCommand cmd;
                cmd.pipelineState = facePso;
                cmd.vertexBuffer = geo->vertexBuffer.get();
                cmd.indexBuffer = geo->indexBuffer.get();
                cmd.indexCount = geo->indexCount;
                cmd.instanceCount = 1;
                cmd.topology = faceMesh->topology;
                cmd.objectUboOffset = nextObjectOffset;
                cmd.materialUboOffset = matCache.materialGpuOffset(0);
                cmd.worldTransform = proxy.worldTransform;
                cmd.pickId = proxy.entity.index();
                cmd.selected = proxy.selected;
                face_cmds_.push_back(std::move(cmd));
                nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
            }
        }

        if (edgeMesh && !edgeMesh->empty()) {
            if (!gpu.edgeGeometry(key))
                gpu.uploadEdgeMesh(key, *edgeMesh);

            if (const auto* geo = gpu.edgeGeometry(key)) {
                engine::MeshDrawCommand cmd;
                cmd.pipelineState = edgePso;
                cmd.vertexBuffer = geo->vertexBuffer.get();
                cmd.indexBuffer = geo->indexBuffer.get();
                cmd.indexCount = geo->indexCount;
                cmd.instanceCount = 1;
                cmd.topology = edgeMesh->topology;
                cmd.objectUboOffset = nextObjectOffset;
                cmd.worldTransform = proxy.worldTransform;
                cmd.pickId = proxy.entity.index();
                cmd.selected = proxy.selected;
                cmd.isEdge = true;
                edge_cmds_.push_back(std::move(cmd));
                nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
            }
        }
    });
}

void DrawCommandBuilder::clear() {
    face_cmds_.clear();
    edge_cmds_.clear();
}

} // namespace mulan::view
