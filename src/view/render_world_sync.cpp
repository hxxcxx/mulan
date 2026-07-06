#include "render_world_sync.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/view/render_scene.h>
#include <mulan/view/scene_proxy.h>

#include <utility>

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

    desc.material = engine::Material::defaultPBR();
    desc.material.name = material->name();
    const auto& color = material->baseColorFactor();
    desc.material.baseColor = { color.x, color.y, color.z };
    desc.material.alpha = color.w;
    desc.material.metallic = material->metallic();
    desc.material.roughness = material->roughness();
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

engine::RenderBucket bucketFor(asset::DrawableRole role) {
    return role == asset::DrawableRole::Wire ? engine::RenderBucket::Edge : engine::RenderBucket::Surface;
}

uint64_t drawableGeometryKey(asset::AssetId geometry, size_t drawableIndex) {
    return geometry.value ^ ((static_cast<uint64_t>(drawableIndex) + 1u) << 32u);
}

}  // namespace

void RenderWorldSync::rebuild(const RenderScene& scene, const asset::AssetLibrary& assets, engine::RenderWorld& world,
                              engine::RenderResourcePrepareList* prepare) const {
    world.clear();
    if (prepare) {
        prepare->clear();
    }

    std::unordered_map<uint64_t, engine::GeometryHandle> geometryHandles;
    std::unordered_map<uint64_t, engine::RenderMaterialHandle> materialHandles;
    std::vector<asset::Drawable> drawables;
    scene.forEachProxy([&](const SceneProxy& proxy) {
        if (!proxy.visible || !proxy.geometry) {
            return;
        }

        const auto* asset = assets.asset(proxy.geometry);
        const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(asset);
        if (!geometry) {
            return;
        }

        drawables.clear();
        geometry->collectDrawables(drawables);

        engine::RenderObjectDesc object;
        object.externalId = proxy.entity.index();
        object.worldTransform = proxy.worldTransform;
        object.worldBounds = proxy.worldBounds;
        object.visible = proxy.visible;
        object.selected = proxy.selected;

        for (size_t i = 0; i < drawables.size(); ++i) {
            const auto& drawable = drawables[i];
            if (!drawable.mesh || drawable.mesh->empty()) {
                continue;
            }

            const uint64_t geometryKey = drawableGeometryKey(proxy.geometry, i);
            auto geometryIt = geometryHandles.find(geometryKey);
            if (geometryIt == geometryHandles.end()) {
                engine::RenderGeometryDesc geometryDesc;
                geometryDesc.resourceKey = engine::makeAssetGpuKey(geometryKey);  // 资产身份 key，跨帧稳定
                geometryDesc.topology = drawable.mesh->topology;                  // 冗余标量，避免渲染端解引用
                geometryDesc.empty = drawable.mesh->empty();
                if (prepare) {
                    prepare->addGeometry(geometryDesc.resourceKey, drawable.mesh);
                }
                geometryIt = geometryHandles.emplace(geometryKey, world.addGeometry(std::move(geometryDesc))).first;
            }

            const uint64_t materialKey = drawable.material.value;
            auto materialIt = materialHandles.find(materialKey);
            if (materialIt == materialHandles.end()) {
                materialIt =
                        materialHandles.emplace(materialKey, world.addMaterial(materialDesc(assets, drawable.material)))
                                .first;
            }

            object.drawables.push_back(engine::RenderObjectDrawable{
                    .geometry = geometryIt->second,
                    .material = materialIt->second,
                    .bucket = bucketFor(drawable.role),
                    .sourceDrawableIndex = i,
            });
        }

        if (!object.drawables.empty()) {
            world.addObject(std::move(object));
        }
    });
}

}  // namespace mulan::view
