#include "render_world_sync.h"
#include "../render_scene.h"
#include "../scene_proxy.h"
#include "../../core/preview_layer.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/core/profiling/profile.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mulan::view {
namespace {

using AssetRevisionMap = std::unordered_map<asset::AssetId, uint64_t>;
using detail::GeometryResourceCandidate;
using detail::GeometryResourceCandidateMap;
using detail::TextureResourceCandidateMap;

void observeAssetReference(asset::AssetId id, const asset::Asset* source, AssetRevisionMap& revisions) {
    if (!id) {
        return;
    }
    // 0 是“引用存在但资产尚未出现”的等待版本；有效资产 revision 从 1 开始。
    // 这样资产创建事件只会唤醒真实等待者，无关成员变化不会触发 world 重建。
    revisions.insert_or_assign(id, source ? source->revision() : 0);
}

engine::RenderTextureDesc textureDesc(const asset::AssetLibrary& assets, asset::AssetId materialId,
                                      asset::AssetId (asset::MaterialAsset::*texGetter)() const,
                                      bool (asset::MaterialAsset::*srgbGetter)() const,
                                      engine::ResourceDomainId assetDomain, AssetRevisionMap& revisions) {
    if (!materialId)
        return {};

    const auto* material = dynamic_cast<const asset::MaterialAsset*>(assets.asset(materialId));
    if (!material)
        return {};

    const asset::AssetId textureId = (material->*texGetter)();
    if (!textureId)
        return {};

    const auto* texture = dynamic_cast<const asset::TextureAsset*>(assets.asset(textureId));
    observeAssetReference(textureId, texture, revisions);
    if (!texture)
        return {};
    if (!texture->hasImage())
        return {};

    engine::RenderTextureDesc desc;
    desc.resourceKey = engine::makeRenderResourceKey(assetDomain, textureId.value, engine::RenderResourceKind::Texture);
    desc.image = texture->image();
    desc.contentRevision = texture->revision();
    desc.srgb = (material->*srgbGetter)();
    // 临时关闭资产纹理 Mip 生成，用于确认同步 CPU 降采样对模型打开耗时的影响。
    desc.generateMips = false;
    return desc;
}

engine::RenderMaterialDesc materialDesc(const asset::AssetLibrary& assets, asset::AssetId materialId,
                                        engine::ResourceDomainId assetDomain, AssetRevisionMap& revisions) {
    engine::RenderMaterialDesc desc;
    // 无材质或材质资产失效时统一回退到稳定的内置身份，避免 world generation 污染缓存键。
    desc.resourceKey = engine::defaultRenderMaterialResourceKey();
    if (!materialId) {
        return desc;
    }

    const auto* material = dynamic_cast<const asset::MaterialAsset*>(assets.asset(materialId));
    observeAssetReference(materialId, material, revisions);
    if (!material) {
        return desc;
    }

    desc.resourceKey =
            engine::makeRenderResourceKey(assetDomain, materialId.value, engine::RenderResourceKind::Material);
    desc.material = engine::Material::defaultPBR();
    desc.material.name = material->name();
    desc.material.shadingModel = material->shadingModel();
    const auto& color = material->baseColorFactor();
    desc.material.baseColor = { color.x, color.y, color.z };
    desc.material.alpha = color.w;
    desc.material.ambient = material->ambientFactor();
    desc.material.specular = material->specularFactor();
    desc.material.shininess = material->shininess();
    desc.material.metallic = material->metallic();
    desc.material.roughness = material->roughness();
    desc.material.emissive = material->emissiveFactor();
    desc.material.emissiveStrength = material->emissiveStrength();
    desc.material.alphaMode = material->alphaMode();
    desc.material.alphaCutoff = material->alphaCutoff();
    desc.material.doubleSided = material->doubleSided();
    desc.baseColorTexture = textureDesc(assets, materialId, &asset::MaterialAsset::baseColorTexture,
                                        &asset::MaterialAsset::baseColorTextureSrgb, assetDomain, revisions);
    desc.normalTexture = textureDesc(assets, materialId, &asset::MaterialAsset::normalTexture,
                                     &asset::MaterialAsset::normalTextureSrgb, assetDomain, revisions);
    desc.metallicRoughnessTexture =
            textureDesc(assets, materialId, &asset::MaterialAsset::metallicRoughnessTexture,
                        &asset::MaterialAsset::metallicRoughnessTextureSrgb, assetDomain, revisions);
    desc.emissiveTexture = textureDesc(assets, materialId, &asset::MaterialAsset::emissiveTexture,
                                       &asset::MaterialAsset::emissiveTextureSrgb, assetDomain, revisions);
    desc.ambientOcclusionTexture = textureDesc(assets, materialId, &asset::MaterialAsset::occlusionTexture,
                                               &asset::MaterialAsset::occlusionTextureSrgb, assetDomain, revisions);
    desc.ambientTexture = textureDesc(assets, materialId, &asset::MaterialAsset::ambientTexture,
                                      &asset::MaterialAsset::ambientTextureSrgb, assetDomain, revisions);
    desc.specularTexture = textureDesc(assets, materialId, &asset::MaterialAsset::specularTexture,
                                       &asset::MaterialAsset::specularTextureSrgb, assetDomain, revisions);
    desc.shininessTexture = textureDesc(assets, materialId, &asset::MaterialAsset::shininessTexture,
                                        &asset::MaterialAsset::shininessTextureSrgb, assetDomain, revisions);
    desc.opacityTexture = textureDesc(assets, materialId, &asset::MaterialAsset::opacityTexture,
                                      &asset::MaterialAsset::opacityTextureSrgb, assetDomain, revisions);

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
    if (hasTexture(desc.ambientTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasAmbient;
    if (hasTexture(desc.specularTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasSpecular;
    if (hasTexture(desc.shininessTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasShininess;
    if (hasTexture(desc.opacityTexture))
        desc.material.textureSlots |= engine::TextureSlotFlags::HasOpacity;

    return desc;
}

void accumulate(RenderItemDiagnostics& dst, const RenderItemDiagnostics& src) {
    dst.accepted += src.accepted;
    dst.rejectedEmpty += src.rejectedEmpty;
    dst.rejectedTopology += src.rejectedTopology;
    dst.rejectedLayout += src.rejectedLayout;
}

engine::RenderMaterialDesc previewMaterialDesc(PreviewVisualRole role, engine::ResourceDomainId previewDomain) {
    engine::RenderMaterialDesc desc;
    desc.resourceKey = engine::makeRenderResourceKey(previewDomain, RenderItemBuilder::previewMaterialKey(role),
                                                     engine::RenderResourceKind::PreviewMaterial);
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

engine::RenderMaterialHandle previewMaterialForRole(PreviewVisualRole role,
                                                    const std::array<engine::RenderMaterialHandle, 4>& materials) {
    return materials[previewVisualRoleIndex(role)];
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

void appendPreviewDrawables(const PreviewLayer& preview, engine::ResourceDomainId previewDomain,
                            engine::RenderWorld& world, GeometryResourceCandidateMap& resources,
                            const std::array<engine::RenderMaterialHandle, 4>& materials, RenderWorldSyncStats& stats) {
    const auto& drawables = preview.drawables();
    if (drawables.empty())
        return;

    std::vector<RenderItem> items;
    RenderItemDiagnostics diagnostics;
    RenderItemBuilder::buildPreviewItems(std::span<const PreviewDrawable>{ drawables.data(), drawables.size() }, items,
                                         &diagnostics);
    accumulate(stats.previewItems, diagnostics);

    engine::RenderObjectDesc object;
    object.pickId = engine::PickId::invalid();
    object.worldBounds = math::AABB3::empty();
    for (const RenderItem& item : items) {
        const graphics::Mesh& mesh = *item.mesh;
        engine::RenderGeometryDesc desc;
        desc.resourceKey = engine::makeRenderResourceKey(previewDomain, item.geometryKey,
                                                         engine::RenderResourceKind::PreviewGeometry);
        desc.topology = mesh.topology;
        desc.vertexLayout = mesh.layout;
        desc.empty = mesh.empty();
        resources.try_emplace(desc.resourceKey,
                              GeometryResourceCandidate{ .mesh = &mesh, .sourceRevision = preview.generation() });
        if (!mesh.bounds.isEmpty())
            object.worldBounds.expand(mesh.bounds);
        object.drawables.push_back(engine::RenderObjectDrawable{
                .geometry = world.addGeometry(std::move(desc)),
                .material = previewMaterialForRole(item.previewRole, materials),
                .bucket = item.bucket,
                .sourceDrawableIndex = item.sourceDrawableIndex,
        });
    }
    if (!object.drawables.empty()) {
        world.addObject(std::move(object));
        ++stats.previewObjectCount;
        ++stats.addedObjectCount;
        ++stats.patchedObjectCount;
    }
}

void appendPreviewReferences(const PreviewLayer& preview, const RenderScene& scene, const asset::AssetLibrary& assets,
                             engine::ResourceDomainId assetDomain, engine::RenderWorld& world,
                             const std::array<engine::RenderMaterialHandle, 4>& materials,
                             AssetRevisionMap& referencedAssets, RenderWorldSyncStats& stats) {
    std::unordered_map<uint64_t, engine::GeometryHandle> geometryHandles;
    std::vector<asset::Drawable> drawables;
    std::vector<RenderItem> items;
    for (const PreviewReference& reference : preview.references()) {
        if (!reference.valid())
            continue;
        const SceneProxy* proxy = scene.proxy(reference.entity);
        if (!proxy || !proxy->visible || !proxy->geometry)
            continue;
        const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(assets.asset(proxy->geometry));
        observeAssetReference(proxy->geometry, geometry, referencedAssets);
        if (!geometry) {
            ++stats.missingGeometryAssetCount;
            continue;
        }

        drawables.clear();
        geometry->collectDrawables(drawables);
        RenderItemDiagnostics diagnostics;
        RenderItemBuilder::buildSceneItems(proxy->geometry,
                                           std::span<const asset::Drawable>{ drawables.data(), drawables.size() },
                                           items, &diagnostics);
        accumulate(stats.previewItems, diagnostics);

        engine::RenderObjectDesc object;
        object.pickId = engine::PickId::invalid();
        object.worldTransform = reference.overrideWorldTransform ? reference.worldTransform : proxy->worldTransform;
        object.worldBounds = math::AABB3::empty();
        object.visible = reference.visible;
        for (const RenderItem& item : items) {
            const std::optional<engine::RenderBucket> bucket = overlayBucketForReference(item.bucket);
            if (!bucket)
                continue;
            const graphics::Mesh& mesh = *item.mesh;
            auto known = geometryHandles.find(item.geometryKey);
            if (known == geometryHandles.end()) {
                engine::RenderGeometryDesc desc;
                desc.resourceKey = engine::makeRenderResourceKey(assetDomain, proxy->geometry.value,
                                                                 engine::RenderResourceKind::Geometry,
                                                                 static_cast<uint32_t>(item.sourceDrawableIndex));
                desc.topology = mesh.topology;
                desc.vertexLayout = mesh.layout;
                desc.empty = mesh.empty();
                known = geometryHandles.emplace(item.geometryKey, world.addGeometry(std::move(desc))).first;
            }
            if (!mesh.bounds.isEmpty())
                object.worldBounds.expand(mesh.bounds.transformed(object.worldTransform));
            object.drawables.push_back(engine::RenderObjectDrawable{
                    .geometry = known->second,
                    .material = previewMaterialForRole(reference.role, materials),
                    .bucket = *bucket,
                    .sourceDrawableIndex = item.sourceDrawableIndex,
            });
        }
        if (!object.drawables.empty()) {
            world.addObject(std::move(object));
            ++stats.previewObjectCount;
            ++stats.addedObjectCount;
            ++stats.patchedObjectCount;
        }
    }
}

}  // namespace

struct RenderWorldSync::SceneState {
    struct GeometryEntry {
        engine::GeometryHandle handle;
        engine::RenderGeometryDesc desc;
        GeometryResourceCandidate resource;
        size_t referenceCount = 0;
    };

    struct MaterialEntry {
        engine::RenderMaterialHandle handle;
        engine::RenderMaterialDesc desc;
        size_t referenceCount = 0;
    };

    struct ObjectEntry {
        engine::RenderObjectId id;
        std::unordered_set<uint64_t> geometryKeys;
        std::unordered_set<uint64_t> materialKeys;
        std::unordered_set<asset::AssetId> dependencies;
    };

    RenderSceneChangeCursor cursor;
    std::unordered_map<scene::EntityId, ObjectEntry> objects;
    std::unordered_map<uint64_t, GeometryEntry> geometries;
    std::unordered_map<uint64_t, MaterialEntry> materials;
    std::unordered_map<asset::AssetId, std::unordered_set<scene::EntityId>> assetUsers;
};

RenderWorldSync::RenderWorldSync() : scene_state_(std::make_unique<SceneState>()) {
}

RenderWorldSync::~RenderWorldSync() = default;

void RenderWorldSync::rebuildScene(const RenderScene& scene, const asset::AssetLibrary& assets,
                                   engine::ResourceDomainId assetDomain, engine::RenderWorld& world,
                                   engine::RenderResourcePrepareList* prepare) {
    MULAN_PROFILE_ZONE();

    last_stats_.reset();
    if (prepare) {
        prepare->clear();
    }

    SceneState& state = *scene_state_;
    const RenderSceneChangeSet changes = scene.readChanges(state.cursor);
    const asset::AssetChangeSet assetChanges = assets.readChanges(asset_change_cursor_);
    const bool fullRebuild = changes.requiresFullResync() || assetChanges.requiresFullResync();
    last_stats_.fullRebuild = fullRebuild;
    if (fullRebuild) {
        world.clear();
        state = {};
        referenced_asset_revisions_.clear();
    }

    struct PendingDrawable {
        uint64_t geometryKey = 0;
        uint64_t materialKey = 0;
        engine::RenderBucket bucket = engine::RenderBucket::Surface;
        size_t sourceDrawableIndex = 0;
    };
    struct PendingProjection {
        engine::RenderObjectDesc object;
        std::vector<PendingDrawable> drawables;
        std::unordered_map<uint64_t, engine::RenderGeometryDesc> geometries;
        std::unordered_map<uint64_t, GeometryResourceCandidate> geometryResources;
        std::unordered_map<uint64_t, engine::RenderMaterialDesc> materials;
        std::unordered_set<asset::AssetId> dependencies;
    };

    bool resourceStateChanged = fullRebuild || resource_delta_.invalidated();

    auto removeDependencyLinks = [&](scene::EntityId entity, const SceneState::ObjectEntry& entry) {
        for (asset::AssetId dependency : entry.dependencies) {
            auto users = state.assetUsers.find(dependency);
            if (users == state.assetUsers.end()) {
                continue;
            }
            users->second.erase(entity);
            if (users->second.empty()) {
                state.assetUsers.erase(users);
                referenced_asset_revisions_.erase(dependency);
            }
        }
    };

    auto removeProjection = [&](scene::EntityId entity, SceneState::ObjectEntry& entry) {
        world.removeObject(entry.id);
        ++last_stats_.removedObjectCount;
        for (uint64_t key : entry.geometryKeys) {
            auto resource = state.geometries.find(key);
            if (resource != state.geometries.end() && --resource->second.referenceCount == 0) {
                world.removeGeometry(resource->second.handle);
                state.geometries.erase(resource);
                resourceStateChanged = true;
            }
        }
        for (uint64_t key : entry.materialKeys) {
            auto material = state.materials.find(key);
            if (material != state.materials.end() && --material->second.referenceCount == 0) {
                world.removeMaterial(material->second.handle);
                state.materials.erase(material);
                resourceStateChanged = true;
            }
        }
        removeDependencyLinks(entity, entry);
    };

    auto patchEntity = [&](scene::EntityId entity) {
        ++last_stats_.patchedObjectCount;
        auto previous = state.objects.find(entity);
        const SceneProxy* proxy = scene.proxy(entity);
        const auto* geometry = proxy && proxy->geometry
                                       ? dynamic_cast<const asset::GeometryAsset*>(assets.asset(proxy->geometry))
                                       : nullptr;
        if (!geometry) {
            if (previous != state.objects.end()) {
                removeProjection(entity, previous->second);
                state.objects.erase(previous);
            }
            return;
        }

        PendingProjection pending;
        pending.object.pickId = proxy->pickId;
        pending.object.worldTransform = proxy->worldTransform;
        // RenderSubmissionBuilder 允许只收到资产内容事件而未先同步 RenderScene。
        // 完整投影因此以当前 GeometryAsset bounds 为准，避免编译器按旧 proxy bounds
        // 错误剔除一帧；纯位姿快路径仍只使用 SceneProxy 缓存，不触碰资产。
        pending.object.worldBounds = geometry->localBounds().transformed(proxy->worldTransform);
        pending.object.visible = proxy->visible;
        pending.dependencies.insert(geometry->id());

        std::vector<asset::Drawable> drawables;
        std::vector<RenderItem> renderItems;
        geometry->collectDrawables(drawables);
        RenderItemDiagnostics diagnostics;
        RenderItemBuilder::buildSceneItems(proxy->geometry,
                                           std::span<const asset::Drawable>{ drawables.data(), drawables.size() },
                                           renderItems, &diagnostics);
        accumulate(last_stats_.sceneItems, diagnostics);
        for (const RenderItem& item : renderItems) {
            const graphics::Mesh& mesh = *item.mesh;
            engine::RenderGeometryDesc geometryDesc;
            geometryDesc.resourceKey = engine::makeRenderResourceKey(assetDomain, proxy->geometry.value,
                                                                     engine::RenderResourceKind::Geometry,
                                                                     static_cast<uint32_t>(item.sourceDrawableIndex));
            geometryDesc.topology = mesh.topology;
            geometryDesc.vertexLayout = mesh.layout;
            geometryDesc.empty = mesh.empty();
            pending.geometries.insert_or_assign(item.geometryKey, geometryDesc);
            pending.geometryResources.insert_or_assign(
                    item.geometryKey,
                    GeometryResourceCandidate{ .mesh = &mesh, .sourceRevision = geometry->revision() });

            const uint64_t materialKey = item.material.value;
            if (!pending.materials.contains(materialKey)) {
                AssetRevisionMap materialDependencies;
                engine::RenderMaterialDesc desc =
                        materialDesc(assets, item.material, assetDomain, materialDependencies);
                for (const auto& [id, revision] : materialDependencies) {
                    (void) revision;
                    pending.dependencies.insert(id);
                }
                pending.materials.emplace(materialKey, std::move(desc));
            }
            pending.drawables.push_back(
                    PendingDrawable{ item.geometryKey, materialKey, item.bucket, item.sourceDrawableIndex });
        }

        if (pending.drawables.empty()) {
            if (previous != state.objects.end()) {
                removeProjection(entity, previous->second);
                state.objects.erase(previous);
            }
            return;
        }

        SceneState::ObjectEntry nextEntry;
        if (previous != state.objects.end()) {
            nextEntry.id = previous->second.id;
        }
        for (const auto& [key, desc] : pending.geometries) {
            nextEntry.geometryKeys.insert(key);
            auto known = state.geometries.find(key);
            if (known == state.geometries.end()) {
                const engine::GeometryHandle handle = world.addGeometry(desc);
                state.geometries.emplace(
                        key, SceneState::GeometryEntry{ handle, desc, pending.geometryResources.at(key), 1 });
                resourceStateChanged = true;
            } else {
                known->second.desc = desc;
                known->second.resource = pending.geometryResources.at(key);
                world.updateGeometry(known->second.handle, desc);
                if (previous == state.objects.end() || !previous->second.geometryKeys.contains(key)) {
                    ++known->second.referenceCount;
                }
            }
        }
        for (const auto& [key, desc] : pending.materials) {
            nextEntry.materialKeys.insert(key);
            auto known = state.materials.find(key);
            if (known == state.materials.end()) {
                const engine::RenderMaterialHandle handle = world.addMaterial(desc);
                state.materials.emplace(key, SceneState::MaterialEntry{ handle, desc, 1 });
                resourceStateChanged = true;
            } else {
                known->second.desc = desc;
                world.updateMaterial(known->second.handle, desc);
                if (previous == state.objects.end() || !previous->second.materialKeys.contains(key)) {
                    ++known->second.referenceCount;
                }
            }
        }

        if (previous != state.objects.end()) {
            for (uint64_t key : previous->second.geometryKeys) {
                if (nextEntry.geometryKeys.contains(key)) {
                    continue;
                }
                auto resource = state.geometries.find(key);
                if (resource != state.geometries.end() && --resource->second.referenceCount == 0) {
                    world.removeGeometry(resource->second.handle);
                    state.geometries.erase(resource);
                    resourceStateChanged = true;
                }
            }
            for (uint64_t key : previous->second.materialKeys) {
                if (nextEntry.materialKeys.contains(key)) {
                    continue;
                }
                auto material = state.materials.find(key);
                if (material != state.materials.end() && --material->second.referenceCount == 0) {
                    world.removeMaterial(material->second.handle);
                    state.materials.erase(material);
                    resourceStateChanged = true;
                }
            }
            removeDependencyLinks(entity, previous->second);
        }
        nextEntry.dependencies = std::move(pending.dependencies);
        for (asset::AssetId dependency : nextEntry.dependencies) {
            state.assetUsers[dependency].insert(entity);
            observeAssetReference(dependency, assets.asset(dependency), referenced_asset_revisions_);
        }
        for (const PendingDrawable& drawable : pending.drawables) {
            pending.object.drawables.push_back(engine::RenderObjectDrawable{
                    .geometry = state.geometries.at(drawable.geometryKey).handle,
                    .material = state.materials.at(drawable.materialKey).handle,
                    .bucket = drawable.bucket,
                    .sourceDrawableIndex = drawable.sourceDrawableIndex,
            });
        }
        if (previous == state.objects.end()) {
            nextEntry.id = world.addObject(std::move(pending.object));
            state.objects.emplace(entity, std::move(nextEntry));
            ++last_stats_.addedObjectCount;
        } else {
            world.updateObject(nextEntry.id, std::move(pending.object));
            previous->second = std::move(nextEntry);
            ++last_stats_.updatedObjectCount;
        }
    };

    std::unordered_map<scene::EntityId, RenderProxyDirty> entitiesToPatch;
    const auto mergePatch = [&](scene::EntityId entity, RenderProxyDirty dirty) {
        if (dirty != RenderProxyDirty::None) {
            entitiesToPatch[entity] |= dirty;
        }
    };
    if (fullRebuild) {
        scene.forEachProxy([&](const SceneProxy& proxy) { mergePatch(proxy.entity, RenderProxyDirty::Added); });
    } else {
        for (const RenderSceneChange& change : changes.changes) {
            mergePatch(change.entity, change.dirty);
        }
        for (const asset::AssetChange& assetChange : assetChanges.changes) {
            if (!assetChange.asset) {
                for (const auto& [entity, entry] : state.objects) {
                    (void) entry;
                    mergePatch(entity, RenderProxyDirty::Geometry | RenderProxyDirty::Material);
                }
                scene.forEachProxy([&](const SceneProxy& proxy) {
                    mergePatch(proxy.entity, RenderProxyDirty::Geometry | RenderProxyDirty::Material);
                });
                resourceStateChanged = true;
                continue;
            }
            if (const auto users = state.assetUsers.find(assetChange.asset); users != state.assetUsers.end()) {
                for (scene::EntityId entity : users->second) {
                    mergePatch(entity, RenderProxyDirty::Geometry | RenderProxyDirty::Material);
                }
                resourceStateChanged = true;
            }
        }
    }

    constexpr RenderProxyDirty fullProjectionMask = RenderProxyDirty::Added | RenderProxyDirty::Removed |
                                                    RenderProxyDirty::Geometry | RenderProxyDirty::Material;
    constexpr RenderProxyDirty spatialPatchMask = RenderProxyDirty::Placement | RenderProxyDirty::Visibility;
    for (const auto& [entity, dirty] : entitiesToPatch) {
        if (!hasAnyRenderProxyDirty(dirty, fullProjectionMask) && hasAnyRenderProxyDirty(dirty, spatialPatchMask)) {
            const auto previous = state.objects.find(entity);
            const SceneProxy* proxy = scene.proxy(entity);
            if (previous != state.objects.end() && proxy &&
                world.updateObjectSpatialState(previous->second.id, proxy->worldTransform, proxy->worldBounds,
                                               proxy->visible)) {
                ++last_stats_.patchedObjectCount;
                ++last_stats_.updatedObjectCount;
                continue;
            }
            // SceneProxy、同步缓存或 RenderWorld 句柄任一缺失时不能猜测状态；
            // 回退完整实体投影，以恢复依赖索引和资源引用计数。
        }
        patchEntity(entity);
    }

    if (resourceStateChanged) {
        GeometryResourceCandidateMap geometryResources;
        for (const auto& [key, entry] : state.geometries) {
            (void) key;
            geometryResources.emplace(entry.desc.resourceKey, entry.resource);
        }
        TextureResourceCandidateMap textureResources;
        for (const auto& [key, entry] : state.materials) {
            (void) key;
            detail::collectMaterialTextureResources(textureResources, entry.desc);
        }
        resource_delta_.build(geometryResources, textureResources, prepare);
    }
    asset_change_cursor_ = assets.currentChangeCursor();
    state.cursor = scene.currentChangeCursor();
    last_stats_.sceneProxyCount = scene.proxyCount();
    last_stats_.missingGeometryAssetCount = scene.lastSyncStats().missingGeometryCount;
    last_stats_.sceneObjectCount = state.objects.size();
    last_stats_.worldObjectCount = world.objectCount();
    last_stats_.worldGeometryCount = world.geometryCount();
    last_stats_.worldMaterialCount = world.materialCount();
}

void RenderWorldSync::rebuildOverlay(const RenderScene* scene, const asset::AssetLibrary* assets,
                                     engine::ResourceDomainId assetDomain, engine::ResourceDomainId previewDomain,
                                     const PreviewLayer* preview, engine::RenderWorld& world,
                                     engine::RenderResourcePrepareList* prepare) {
    MULAN_PROFILE_ZONE();

    last_stats_.reset();
    last_stats_.fullRebuild = true;
    world.clear();
    if (prepare)
        prepare->clear();

    GeometryResourceCandidateMap resources;
    AssetRevisionMap referencedAssets;
    if (preview && !preview->empty()) {
        std::array<engine::RenderMaterialHandle, kPreviewVisualRoleCount> materials;
        for (const PreviewVisualRole role : { PreviewVisualRole::Tool, PreviewVisualRole::Snap, PreviewVisualRole::Grip,
                                              PreviewVisualRole::GripHot }) {
            materials[previewVisualRoleIndex(role)] = world.addMaterial(previewMaterialDesc(role, previewDomain));
        }
        appendPreviewDrawables(*preview, previewDomain, world, resources, materials, last_stats_);
        if (scene && assets) {
            appendPreviewReferences(*preview, *scene, *assets, assetDomain, world, materials, referencedAssets,
                                    last_stats_);
        }
    }

    resource_delta_.build(resources, {}, prepare);
    referenced_asset_revisions_ = std::move(referencedAssets);
    asset_change_cursor_ = assets ? assets->currentChangeCursor() : asset::AssetChangeCursor{};
    last_stats_.worldObjectCount = world.objectCount();
    last_stats_.worldGeometryCount = world.geometryCount();
    last_stats_.worldMaterialCount = world.materialCount();
}

void RenderWorldSync::rebuildEmpty(engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare) {
    last_stats_.reset();
    world.clear();
    if (prepare) {
        prepare->clear();
    }
    resource_delta_.build({}, {}, prepare);
    referenced_asset_revisions_.clear();
    asset_change_cursor_ = {};
    *scene_state_ = {};
}

bool RenderWorldSync::referencedAssetsChanged(const asset::AssetLibrary& assets) const {
    if (resource_delta_.invalidated()) {
        return true;
    }
    const asset::AssetChangeSet changes = assets.readChanges(asset_change_cursor_);
    if (changes.requiresFullResync()) {
        return true;
    }
    for (const asset::AssetChange& change : changes.changes) {
        if (!change.asset || referenced_asset_revisions_.contains(change.asset)) {
            return true;
        }
    }
    // 全部是无关事件时可以安全确认读取；真正相关的批次必须留给 rebuildScene
    // 在成功更新 world 后提交，不能在这里只检查不应用便提前越过。
    asset_change_cursor_ = changes.cursorAfterApply();
    return false;
}

void RenderWorldSync::reset() {
    last_stats_.reset();
    resource_delta_.reset();
    referenced_asset_revisions_.clear();
    asset_change_cursor_ = {};
    *scene_state_ = {};
}

}  // namespace mulan::view
