#include "draw_command_builder.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/engine/render/render_resource_cache.h>
#include <mulan/engine/render/material/material.h>
#include <mulan/engine/render/material/material_cache.h>
#include <mulan/render_scene/render_scene.h>
#include <mulan/render_scene/scene_proxy.h>

#include <string>

namespace mulan::view {
namespace {

constexpr uint32_t kDefaultMaterialId = 0xFFFF;

engine::AlphaMode toEngineAlphaMode(asset::AlphaMode mode) {
    switch (mode) {
    case asset::AlphaMode::Mask:
        return engine::AlphaMode::Mask;
    case asset::AlphaMode::Blend:
        return engine::AlphaMode::Blend;
    case asset::AlphaMode::Opaque:
    default:
        return engine::AlphaMode::Opaque;
    }
}

engine::Material toEngineMaterial(const asset::MaterialAsset& asset) {
    engine::Material material = engine::Material::defaultPBR();
    material.name = asset.name();
    const auto& color = asset.baseColorFactor();
    material.baseColor = {color.x, color.y, color.z};
    material.alpha = color.w;
    material.metallic = asset.metallic();
    material.roughness = asset.roughness();
    material.alphaMode = toEngineAlphaMode(asset.alphaMode());
    material.doubleSided = asset.doubleSided();
    return material;
}

uint32_t resolveMaterialOffset(const asset::AssetLibrary& assets,
                               asset::AssetId materialId,
                               engine::MaterialCache& cache) {
    if (!materialId) {
        return cache.materialGpuOffset(kDefaultMaterialId);
    }

    const auto* materialAsset = dynamic_cast<const asset::MaterialAsset*>(assets.asset(materialId));
    if (!materialAsset) {
        return cache.materialGpuOffset(kDefaultMaterialId);
    }

    const std::string cacheName = "asset:" + std::to_string(materialId.value);
    const uint32_t engineMaterialId = cache.registerMaterial(cacheName, toEngineMaterial(*materialAsset));
    return cache.materialGpuOffset(engineMaterialId);
}

uint64_t primitiveGeometryKey(asset::AssetId geometry, size_t primitiveIndex) {
    return geometry.value ^ ((static_cast<uint64_t>(primitiveIndex) + 1u) << 32u);
}

} // namespace

void DrawCommandBuilder::setScene(const render_scene::RenderScene* scene,
                                  const asset::AssetLibrary* assets) {
    scene_ = scene;
    assets_ = assets;
}

void DrawCommandBuilder::rebuild(engine::RenderResourceCache& resources,
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

            const uint64_t key = proxy.geometry.value;
            if (faceMesh && !faceMesh->empty()) {
                if (!resources.faceGeometry(key))
                    resources.uploadFaceGeometry(key, *faceMesh);

                if (const auto* geo = resources.faceGeometry(key)) {
                    engine::MeshDrawCommand cmd;
                    cmd.pipelineState = facePso;
                    cmd.vertexBuffer = geo->vertexBuffer.get();
                    cmd.indexBuffer = geo->indexBuffer.get();
                    cmd.indexCount = geo->indexCount;
                    cmd.instanceCount = 1;
                    cmd.topology = faceMesh->topology;
                    cmd.objectUboOffset = nextObjectOffset;
                    cmd.materialUboOffset = matCache.materialGpuOffset(kDefaultMaterialId);
                    cmd.worldTransform = proxy.worldTransform;
                    cmd.pickId = proxy.entity.index();
                    cmd.selected = proxy.selected;
                    face_cmds_.push_back(std::move(cmd));
                    nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
                }
            }

            if (edgeMesh && !edgeMesh->empty()) {
                if (!resources.edgeGeometry(key))
                    resources.uploadEdgeGeometry(key, *edgeMesh);

                if (const auto* geo = resources.edgeGeometry(key)) {
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
        } else if (const auto* meshAsset = dynamic_cast<const asset::MeshAsset*>(asset)) {
            size_t primitiveIndex = 0;
            for (const auto& primitive : meshAsset->primitives()) {
                const auto& mesh = primitive.mesh;
                if (mesh.empty()) {
                    ++primitiveIndex;
                    continue;
                }

                const uint64_t key = primitiveGeometryKey(proxy.geometry, primitiveIndex);
                if (!resources.faceGeometry(key))
                    resources.uploadFaceGeometry(key, mesh);

                if (const auto* geo = resources.faceGeometry(key)) {
                    engine::MeshDrawCommand cmd;
                    cmd.pipelineState = facePso;
                    cmd.vertexBuffer = geo->vertexBuffer.get();
                    cmd.indexBuffer = geo->indexBuffer.get();
                    cmd.indexCount = geo->indexCount;
                    cmd.instanceCount = 1;
                    cmd.topology = mesh.topology;
                    cmd.objectUboOffset = nextObjectOffset;
                    cmd.materialUboOffset = resolveMaterialOffset(*assets_, primitive.material, matCache);
                    cmd.worldTransform = proxy.worldTransform;
                    cmd.pickId = proxy.entity.index();
                    cmd.selected = proxy.selected;
                    face_cmds_.push_back(std::move(cmd));
                    nextObjectOffset += engine::MeshDrawCommand::kObjectUboStride;
                }

                ++primitiveIndex;
            }
        }
    });
}

void DrawCommandBuilder::clear() {
    face_cmds_.clear();
    edge_cmds_.clear();
}

} // namespace mulan::view
