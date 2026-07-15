#include "scene_sync/render_world_sync.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/view/core/preview_layer.h>
#include <mulan/view/scene_sync/render_scene.h>
#include <mulan/view/scene_sync/scene_proxy.h>

#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mulan::view {
namespace {

engine::RenderTextureDesc textureDesc(const asset::AssetLibrary& assets, asset::AssetId materialId,
                                      asset::AssetId (asset::MaterialAsset::*texGetter)() const,
                                      bool (asset::MaterialAsset::*srgbGetter)() const) {
    if (!materialId)
        return {};

    const auto* material = dynamic_cast<const asset::MaterialAsset*>(assets.asset(materialId));
    if (!material)
        return {};

    const asset::AssetId textureId = (material->*texGetter)();
    if (!textureId)
        return {};

    const auto* texture = dynamic_cast<const asset::TextureAsset*>(assets.asset(textureId));
    if (!texture)
        return {};
    if (!texture->hasImage())
        return {};

    engine::RenderTextureDesc desc;
    desc.resourceKey = engine::makeAssetGpuKey(textureId.value);
    desc.image = texture->image();
    desc.srgb = (material->*srgbGetter)();
    return desc;
}

engine::RenderMaterialDesc materialDesc(const asset::AssetLibrary& assets, asset::AssetId materialId) {
    engine::RenderMaterialDesc desc;
    if (!materialId) {
        return desc;
    }

    const auto* material = dynamic_cast<const asset::MaterialAsset*>(assets.asset(materialId));
    if (!material) {
        return desc;
    }

    desc.resourceKey = engine::makeAssetGpuKey(materialId.value);
    desc.material = engine::Material::defaultPBR();
    desc.material.name = material->name();
    const auto& color = material->baseColorFactor();
    desc.material.baseColor = { color.x, color.y, color.z };
    desc.material.alpha = color.w;
    desc.material.metallic = material->metallic();
    desc.material.roughness = material->roughness();
    desc.material.emissive = material->emissiveFactor();
    desc.material.emissiveStrength = material->emissiveStrength();
    desc.material.alphaMode = material->alphaMode();
    desc.material.doubleSided = material->doubleSided();
    desc.baseColorTexture = textureDesc(assets, materialId, &asset::MaterialAsset::baseColorTexture,
                                        &asset::MaterialAsset::baseColorTextureSrgb);
    desc.normalTexture = textureDesc(assets, materialId, &asset::MaterialAsset::normalTexture,
                                     &asset::MaterialAsset::normalTextureSrgb);
    desc.metallicRoughnessTexture = textureDesc(assets, materialId, &asset::MaterialAsset::metallicRoughnessTexture,
                                                &asset::MaterialAsset::metallicRoughnessTextureSrgb);
    desc.emissiveTexture = textureDesc(assets, materialId, &asset::MaterialAsset::emissiveTexture,
                                       &asset::MaterialAsset::emissiveTextureSrgb);
    desc.ambientOcclusionTexture = textureDesc(assets, materialId, &asset::MaterialAsset::occlusionTexture,
                                               &asset::MaterialAsset::occlusionTextureSrgb);

    // 点亮 Material::textureSlots —— MaterialGPU::fromMaterial 据此生成 textureFlags 位掩码，
    // shader 用 (flags & TF_*) 决定是否采样。未点亮则纹理虽绑定到 descriptor 但被跳过。
    // 真实纹理数据由上面的 RenderTextureDesc 槽位携带，这里仅是"有无"标志。
    auto hasTexture = [](const engine::RenderTextureDesc& texture) {
        return static_cast<bool>(texture.resourceKey) && texture.image && texture.image->valid();
    };

    if (hasTexture(desc.baseColorTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasAlbedo;
    if (hasTexture(desc.normalTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasNormal;
    if (hasTexture(desc.metallicRoughnessTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasMetallicRough;
    if (hasTexture(desc.emissiveTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasEmissive;
    if (hasTexture(desc.ambientOcclusionTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasAO;

    return desc;
}

void accumulate(RenderItemDiagnostics& dst, const RenderItemDiagnostics& src) {
    dst.accepted += src.accepted;
    dst.rejectedEmpty += src.rejectedEmpty;
    dst.rejectedTopology += src.rejectedTopology;
    dst.rejectedLayout += src.rejectedLayout;
}

engine::RenderMaterialDesc previewMaterialDesc(PreviewVisualRole role) {
    engine::RenderMaterialDesc desc;
    desc.resourceKey = engine::makeAssetGpuKey(RenderItemBuilder::previewMaterialKey(role));
    switch (role) {
    case PreviewVisualRole::Tool:
        desc.material = engine::Material::unlit(math::Vec3(0.0, 0.58, 1.0));
        desc.material.name = "ToolPreview";
        break;
    case PreviewVisualRole::Snap:
        desc.material = engine::Material::unlit(math::Vec3(1.0, 0.74, 0.16));
        desc.material.name = "SnapPreview";
        break;
    case PreviewVisualRole::Grip:
        desc.material = engine::Material::unlit(math::Vec3(0.2, 1.0, 0.38));
        desc.material.name = "GripPreview";
        break;
    case PreviewVisualRole::GripHot:
        desc.material = engine::Material::unlit(math::Vec3(1.0, 0.95, 0.25));
        desc.material.name = "GripHotPreview";
        break;
    }
    return desc;
}

engine::RenderMaterialHandle previewMaterialForRole(PreviewVisualRole role, engine::RenderMaterialHandle toolMaterial,
                                                    engine::RenderMaterialHandle snapMaterial,
                                                    engine::RenderMaterialHandle gripMaterial,
                                                    engine::RenderMaterialHandle gripHotMaterial) {
    switch (role) {
    case PreviewVisualRole::Tool: return toolMaterial;
    case PreviewVisualRole::Snap: return snapMaterial;
    case PreviewVisualRole::Grip: return gripMaterial;
    case PreviewVisualRole::GripHot: return gripHotMaterial;
    }
    return toolMaterial;
}

std::optional<engine::RenderBucket> overlayBucketForReference(engine::RenderBucket bucket) {
    switch (bucket) {
    case engine::RenderBucket::Surface: return engine::RenderBucket::OverlaySurface;
    case engine::RenderBucket::Edge: return engine::RenderBucket::OverlayEdge;
    case engine::RenderBucket::OverlaySurface:
    case engine::RenderBucket::OverlayEdge:
    case engine::RenderBucket::Gizmo:
    case engine::RenderBucket::Text: return std::nullopt;
    }
    return std::nullopt;
}

void appendPreviewDrawables(const PreviewLayer& preview, engine::RenderWorld& world,
                            engine::RenderResourcePrepareList* prepare, RenderWorldSyncStats& stats,
                            engine::RenderMaterialHandle toolMaterial, engine::RenderMaterialHandle snapMaterial,
                            engine::RenderMaterialHandle gripMaterial, engine::RenderMaterialHandle gripHotMaterial) {
    const auto& drawables = preview.drawables();
    if (drawables.empty()) {
        return;
    }

    engine::RenderObjectDesc object;
    object.pickId = engine::PickId::invalid();
    object.worldTransform = math::Mat4(1.0f);
    object.worldBounds = math::AABB3::empty();
    object.visible = true;
    object.selected = false;

    std::vector<RenderItem> items;
    RenderItemDiagnostics diagnostics;
    RenderItemBuilder::buildPreviewItems(std::span<const PreviewDrawable>{ drawables.data(), drawables.size() }, items,
                                         &diagnostics);
    accumulate(stats.previewItems, diagnostics);
    for (const RenderItem& item : items) {
        const graphics::Mesh& mesh = *item.mesh;

        engine::RenderGeometryDesc geometryDesc;
        geometryDesc.resourceKey = engine::makeAssetGpuKey(item.geometryKey);
        geometryDesc.topology = mesh.topology;
        geometryDesc.vertexLayout = mesh.layout;
        geometryDesc.empty = mesh.empty();
        if (prepare) {
            // 预览 key 按角色槽位稳定复用；每次预览重建都覆盖同一 GPU 资源。
            prepare->addGeometry(geometryDesc.resourceKey, mesh, true);
        }

        if (!mesh.bounds.isEmpty()) {
            object.worldBounds.expand(mesh.bounds);
        }
        object.drawables.push_back(engine::RenderObjectDrawable{
                .geometry = world.addGeometry(std::move(geometryDesc)),
                .material = previewMaterialForRole(item.previewRole, toolMaterial, snapMaterial, gripMaterial,
                                                   gripHotMaterial),
                .bucket = item.bucket,
                .sourceDrawableIndex = item.sourceDrawableIndex,
        });
    }

    if (!object.drawables.empty()) {
        world.addObject(std::move(object));
        ++stats.previewObjectCount;
    }
}

void appendPreviewReferences(const PreviewLayer& preview, const RenderScene& scene, const asset::AssetLibrary& assets,
                             engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare,
                             RenderWorldSyncStats& stats,
                             std::unordered_map<uint64_t, engine::GeometryHandle>& geometryHandles,
                             bool prepareSceneGeometry, engine::RenderMaterialHandle toolMaterial,
                             engine::RenderMaterialHandle snapMaterial, engine::RenderMaterialHandle gripMaterial,
                             engine::RenderMaterialHandle gripHotMaterial) {
    const auto& references = preview.references();
    if (references.empty()) {
        return;
    }

    std::vector<asset::Drawable> drawables;
    std::vector<RenderItem> renderItems;
    for (const PreviewReference& reference : references) {
        if (!reference.valid()) {
            continue;
        }

        const SceneProxy* proxy = scene.proxy(reference.entity);
        if (!proxy || !proxy->visible || !proxy->geometry) {
            continue;
        }

        const auto* asset = assets.asset(proxy->geometry);
        const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(asset);
        if (!geometry) {
            ++stats.missingGeometryAssetCount;
            continue;
        }

        drawables.clear();
        geometry->collectDrawables(drawables);
        RenderItemDiagnostics diagnostics;
        RenderItemBuilder::buildSceneItems(proxy->geometry,
                                           std::span<const asset::Drawable>{ drawables.data(), drawables.size() },
                                           renderItems, &diagnostics);
        accumulate(stats.previewItems, diagnostics);

        engine::RenderObjectDesc object;
        object.pickId = engine::PickId::invalid();
        object.worldTransform = reference.overrideWorldTransform ? reference.worldTransform : proxy->worldTransform;
        object.worldBounds = math::AABB3::empty();
        object.visible = reference.visible;
        object.selected = false;

        for (const RenderItem& item : renderItems) {
            const graphics::Mesh& mesh = *item.mesh;
            const std::optional<engine::RenderBucket> bucket = overlayBucketForReference(item.bucket);
            if (!bucket) {
                continue;
            }

            auto geometryIt = geometryHandles.find(item.geometryKey);
            if (geometryIt == geometryHandles.end()) {
                engine::RenderGeometryDesc geometryDesc;
                geometryDesc.resourceKey = engine::makeAssetGpuKey(item.geometryKey);
                geometryDesc.topology = mesh.topology;
                geometryDesc.vertexLayout = mesh.layout;
                geometryDesc.empty = mesh.empty();
                if (prepare && prepareSceneGeometry) {
                    prepare->addGeometry(geometryDesc.resourceKey, mesh);
                }
                geometryIt =
                        geometryHandles.emplace(item.geometryKey, world.addGeometry(std::move(geometryDesc))).first;
            }

            if (!mesh.bounds.isEmpty()) {
                object.worldBounds.expand(mesh.bounds.transformed(object.worldTransform));
            }
            object.drawables.push_back(engine::RenderObjectDrawable{
                    .geometry = geometryIt->second,
                    .material = previewMaterialForRole(reference.role, toolMaterial, snapMaterial, gripMaterial,
                                                       gripHotMaterial),
                    .bucket = *bucket,
                    .sourceDrawableIndex = item.sourceDrawableIndex,
            });
        }

        if (!object.drawables.empty()) {
            world.addObject(std::move(object));
            ++stats.previewObjectCount;
        }
    }
}

void appendPreview(const RenderScene& scene, const asset::AssetLibrary& assets, const PreviewLayer* preview,
                   engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare, RenderWorldSyncStats& stats,
                   std::unordered_map<uint64_t, engine::GeometryHandle>& geometryHandles, bool prepareSceneGeometry) {
    if (!preview || preview->empty()) {
        return;
    }

    const engine::RenderMaterialHandle toolMaterial = world.addMaterial(previewMaterialDesc(PreviewVisualRole::Tool));
    const engine::RenderMaterialHandle snapMaterial = world.addMaterial(previewMaterialDesc(PreviewVisualRole::Snap));
    const engine::RenderMaterialHandle gripMaterial = world.addMaterial(previewMaterialDesc(PreviewVisualRole::Grip));
    const engine::RenderMaterialHandle gripHotMaterial =
            world.addMaterial(previewMaterialDesc(PreviewVisualRole::GripHot));

    appendPreviewDrawables(*preview, world, prepare, stats, toolMaterial, snapMaterial, gripMaterial, gripHotMaterial);
    appendPreviewReferences(*preview, scene, assets, world, prepare, stats, geometryHandles, prepareSceneGeometry,
                            toolMaterial, snapMaterial, gripMaterial, gripHotMaterial);
}

}  // namespace

void RenderWorldSync::rebuild(const RenderScene& scene, const asset::AssetLibrary& assets, const PreviewLayer* preview,
                              engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare,
                              bool prepareSceneGeometry, bool forceSceneGeometryUpdate) const {
    last_stats_.reset();
    world.clear();
    if (prepare) {
        prepare->clear();
    }

    std::unordered_map<uint64_t, engine::GeometryHandle> geometryHandles;
    std::unordered_map<uint64_t, engine::RenderMaterialHandle> materialHandles;
    std::vector<asset::Drawable> drawables;
    std::vector<RenderItem> renderItems;
    scene.forEachProxy([&](const SceneProxy& proxy) {
        if (!proxy.visible || !proxy.geometry) {
            return;
        }
        ++last_stats_.sceneProxyCount;

        const auto* asset = assets.asset(proxy.geometry);
        const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(asset);
        if (!geometry) {
            ++last_stats_.missingGeometryAssetCount;
            return;
        }

        drawables.clear();
        geometry->collectDrawables(drawables);
        RenderItemDiagnostics diagnostics;
        RenderItemBuilder::buildSceneItems(proxy.geometry,
                                           std::span<const asset::Drawable>{ drawables.data(), drawables.size() },
                                           renderItems, &diagnostics);
        accumulate(last_stats_.sceneItems, diagnostics);

        engine::RenderObjectDesc object;
        object.pickId = engine::PickId::fromValue(proxy.entity.index());
        object.worldTransform = proxy.worldTransform;
        object.worldBounds = proxy.worldBounds;
        object.visible = proxy.visible;
        object.selected = proxy.selected;

        for (const RenderItem& item : renderItems) {
            const graphics::Mesh& mesh = *item.mesh;

            const uint64_t geometryKey = item.geometryKey;
            auto geometryIt = geometryHandles.find(geometryKey);
            if (geometryIt == geometryHandles.end()) {
                engine::RenderGeometryDesc geometryDesc;
                geometryDesc.resourceKey = engine::makeAssetGpuKey(geometryKey);  // 资产身份 key，跨帧稳定
                geometryDesc.topology = mesh.topology;                            // 冗余标量，避免渲染端解引用
                geometryDesc.vertexLayout = mesh.layout;
                geometryDesc.empty = mesh.empty();
                if (prepare && prepareSceneGeometry) {
                    prepare->addGeometry(geometryDesc.resourceKey, mesh, forceSceneGeometryUpdate);
                }
                geometryIt = geometryHandles.emplace(geometryKey, world.addGeometry(std::move(geometryDesc))).first;
            }

            const uint64_t materialKey = item.material.value;
            auto materialIt = materialHandles.find(materialKey);
            if (materialIt == materialHandles.end()) {
                materialIt =
                        materialHandles.emplace(materialKey, world.addMaterial(materialDesc(assets, item.material)))
                                .first;
            }

            object.drawables.push_back(engine::RenderObjectDrawable{
                    .geometry = geometryIt->second,
                    .material = materialIt->second,
                    .bucket = item.bucket,
                    .sourceDrawableIndex = item.sourceDrawableIndex,
            });
        }

        if (!object.drawables.empty()) {
            world.addObject(std::move(object));
            ++last_stats_.sceneObjectCount;
        }
    });

    appendPreview(scene, assets, preview, world, prepare, last_stats_, geometryHandles, prepareSceneGeometry);
    last_stats_.worldObjectCount = world.objectCount();
    last_stats_.worldGeometryCount = world.geometryCount();
    last_stats_.worldMaterialCount = world.materialCount();
}

}  // namespace mulan::view
