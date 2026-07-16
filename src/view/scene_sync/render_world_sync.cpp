#include "scene_sync/render_world_sync.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/view/scene_sync/render_scene.h>
#include <mulan/view/scene_sync/scene_proxy.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mulan::view {
namespace {

using AssetRevisionMap = std::unordered_map<asset::AssetId, uint64_t>;
using GeometryContentRevision = std::array<uint64_t, 3>;

struct GeometryResourceCandidate {
    const graphics::Mesh* mesh = nullptr;
    uint64_t sourceRevision = 0;
};

using GeometryResourceCandidateMap = std::unordered_map<engine::RenderResourceKey, GeometryResourceCandidate>;

struct TextureResourceCandidate {
    std::shared_ptr<const core::Image> image;
    uint64_t contentRevision = 0;
};

using TextureResourceCandidateMap = std::unordered_map<engine::RenderTextureResourceKey, TextureResourceCandidate,
                                                       engine::RenderTextureResourceKeyHash>;
using TextureRevisionMap =
        std::unordered_map<engine::RenderTextureResourceKey, uint64_t, engine::RenderTextureResourceKeyHash>;

void hashValue(uint64_t& hash, uint64_t value) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    for (size_t byte = 0; byte < sizeof(value); ++byte) {
        hash ^= (value >> (byte * 8u)) & 0xffu;
        hash *= kFnvPrime;
    }
}

void hashBytes(uint64_t& hash, std::span<const std::byte> bytes) {
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    hashValue(hash, bytes.size());
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<uint8_t>(value);
        hash *= kFnvPrime;
    }
}

std::pair<uint64_t, uint64_t> meshContentFingerprint(const graphics::Mesh& mesh) {
    uint64_t primary = 14695981039346656037ull;
    uint64_t secondary = 7809847782465536322ull;
    auto mixValue = [&](uint64_t value) {
        hashValue(primary, value);
        hashValue(secondary, value ^ 0x9e3779b97f4a7c15ull);
    };
    auto mixBytes = [&](std::span<const std::byte> bytes) {
        hashBytes(primary, bytes);
        hashBytes(secondary, bytes);
    };

    mixValue(mesh.layout.stride());
    mixValue(mesh.layout.attrCount());
    mixValue(mesh.layout.bufferCount());
    for (const graphics::VertexAttribute& attribute : mesh.layout.attributes()) {
        mixValue(static_cast<uint64_t>(attribute.semantic));
        mixValue(static_cast<uint64_t>(attribute.format));
        mixValue(attribute.offset);
        mixValue(attribute.bufferSlot);
    }
    mixValue(static_cast<uint64_t>(mesh.indexType));
    mixValue(static_cast<uint64_t>(mesh.topology));
    mixBytes(mesh.vertices);
    mixBytes(mesh.indices);
    return { primary, secondary };
}

bool geometryContentChanged(const GeometryContentRevision& previous, const GeometryContentRevision& current) {
    return previous[1] != current[1] || previous[2] != current[2];
}

void observeAssetReference(asset::AssetId id, const asset::Asset* source, AssetRevisionMap& revisions) {
    if (!id) {
        return;
    }
    // 0 是“引用存在但资产尚未出现”的等待版本；有效资产 revision 从 1 开始。
    // 这样资产创建事件只会唤醒真实等待者，无关成员变化不会触发 world 重建。
    revisions.insert_or_assign(id, source ? source->revision() : 0);
}

engine::RenderTextureResourceKey textureResourceKey(const engine::RenderTextureDesc& texture) {
    return engine::RenderTextureResourceKey{
        .resourceKey = texture.resourceKey,
        .srgb = texture.srgb,
        .generateMips = texture.generateMips,
    };
}

void collectTextureResource(TextureResourceCandidateMap& resources, const engine::RenderTextureDesc& texture) {
    const engine::RenderTextureResourceKey identity = textureResourceKey(texture);
    if (!identity || !texture.image || !texture.image->valid()) {
        return;
    }
    resources.insert_or_assign(identity, TextureResourceCandidate{
                                                 .image = texture.image,
                                                 .contentRevision = texture.contentRevision,
                                         });
}

void collectMaterialTextureResources(TextureResourceCandidateMap& resources,
                                     const engine::RenderMaterialDesc& material) {
    collectTextureResource(resources, material.baseColorTexture);
    collectTextureResource(resources, material.normalTexture);
    collectTextureResource(resources, material.metallicRoughnessTexture);
    collectTextureResource(resources, material.emissiveTexture);
    collectTextureResource(resources, material.ambientOcclusionTexture);
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

bool resourceKeyLess(const engine::RenderResourceKey& lhs, const engine::RenderResourceKey& rhs) {
    if (lhs.domain.value != rhs.domain.value) {
        return lhs.domain.value < rhs.domain.value;
    }
    if (lhs.source != rhs.source) {
        return lhs.source < rhs.source;
    }
    if (lhs.subresource != rhs.subresource) {
        return lhs.subresource < rhs.subresource;
    }
    return lhs.kind < rhs.kind;
}

void buildGeometryResourceDelta(const GeometryResourceCandidateMap& current,
                                std::unordered_map<engine::RenderResourceKey, GeometryContentRevision>& previous,
                                bool forceFullPrepare, engine::RenderResourcePrepareList* prepare) {
    if (!prepare) {
        return;
    }

    std::vector<engine::RenderResourceKey> retiredKeys;
    retiredKeys.reserve(previous.size());
    for (const auto& [key, revision] : previous) {
        (void) revision;
        if (!current.contains(key)) {
            retiredKeys.push_back(key);
        }
    }
    std::ranges::sort(retiredKeys, resourceKeyLess);
    for (const engine::RenderResourceKey key : retiredKeys) {
        prepare->retireGeometry(key);
    }

    std::vector<engine::RenderResourceKey> currentKeys;
    currentKeys.reserve(current.size());
    for (const auto& [key, resource] : current) {
        (void) resource;
        currentKeys.push_back(key);
    }
    std::ranges::sort(currentKeys, resourceKeyLess);

    std::unordered_map<engine::RenderResourceKey, GeometryContentRevision> next;
    next.reserve(current.size());
    for (const engine::RenderResourceKey key : currentKeys) {
        const GeometryResourceCandidate& resource = current.at(key);
        const auto previousIt = previous.find(key);
        const bool existed = previousIt != previous.end();
        GeometryContentRevision currentRevision{ resource.sourceRevision, 0, 0 };
        if (existed && previousIt->second[0] == resource.sourceRevision) {
            // transform/selection/material 等 world-only rebuild 不会扫描大网格字节；
            // 只有新 key 或对应内容源版本变化时才重算该 key 的指纹。
            currentRevision = previousIt->second;
        } else {
            const auto [primary, secondary] = meshContentFingerprint(*resource.mesh);
            currentRevision[1] = primary;
            currentRevision[2] = secondary;
        }
        const bool revisionChanged = existed && geometryContentChanged(previousIt->second, currentRevision);
        if (forceFullPrepare || !existed || revisionChanged) {
            // 全量恢复和内容版本变化都允许覆盖 registry 中可能存在的旧实例。
            prepare->addGeometry(key, *resource.mesh, forceFullPrepare || revisionChanged);
        }
        next.emplace(key, currentRevision);
    }

    previous = std::move(next);
}

bool textureKeyLess(const engine::RenderTextureResourceKey& lhs, const engine::RenderTextureResourceKey& rhs) {
    if (lhs.resourceKey.domain.value != rhs.resourceKey.domain.value) {
        return lhs.resourceKey.domain.value < rhs.resourceKey.domain.value;
    }
    if (lhs.resourceKey.source != rhs.resourceKey.source) {
        return lhs.resourceKey.source < rhs.resourceKey.source;
    }
    if (lhs.resourceKey.subresource != rhs.resourceKey.subresource) {
        return lhs.resourceKey.subresource < rhs.resourceKey.subresource;
    }
    if (lhs.resourceKey.kind != rhs.resourceKey.kind) {
        return lhs.resourceKey.kind < rhs.resourceKey.kind;
    }
    if (lhs.srgb != rhs.srgb) {
        return lhs.srgb < rhs.srgb;
    }
    return lhs.generateMips < rhs.generateMips;
}

void buildTextureResourceDelta(const TextureResourceCandidateMap& current, TextureRevisionMap& previous,
                               bool forceFullPrepare, engine::RenderResourcePrepareList* prepare) {
    if (!prepare) {
        return;
    }

    std::vector<engine::RenderTextureResourceKey> retiredKeys;
    retiredKeys.reserve(previous.size());
    for (const auto& [key, revision] : previous) {
        (void) revision;
        if (!current.contains(key)) {
            retiredKeys.push_back(key);
        }
    }
    std::ranges::sort(retiredKeys, textureKeyLess);
    for (const engine::RenderTextureResourceKey& key : retiredKeys) {
        prepare->retireTexture(key);
    }

    std::vector<engine::RenderTextureResourceKey> currentKeys;
    currentKeys.reserve(current.size());
    for (const auto& [key, resource] : current) {
        (void) resource;
        currentKeys.push_back(key);
    }
    std::ranges::sort(currentKeys, textureKeyLess);

    TextureRevisionMap next;
    next.reserve(current.size());
    for (const engine::RenderTextureResourceKey& key : currentKeys) {
        const TextureResourceCandidate& resource = current.at(key);
        const auto known = previous.find(key);
        const bool changed = known == previous.end() || known->second != resource.contentRevision;
        if (forceFullPrepare || changed) {
            prepare->addTexture(key, resource.image, resource.contentRevision);
        }
        next.emplace(key, resource.contentRevision);
    }
    previous = std::move(next);
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

    bool resourceStateChanged = fullRebuild || force_full_prepare_;

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
        pending.object.selected = false;
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

    const bool forceFullPrepare = force_full_prepare_;
    if (resourceStateChanged) {
        GeometryResourceCandidateMap geometryResources;
        for (const auto& [key, entry] : state.geometries) {
            (void) key;
            geometryResources.emplace(entry.desc.resourceKey, entry.resource);
        }
        TextureResourceCandidateMap textureResources;
        for (const auto& [key, entry] : state.materials) {
            (void) key;
            collectMaterialTextureResources(textureResources, entry.desc);
        }
        buildGeometryResourceDelta(geometryResources, geometry_revisions_, forceFullPrepare, prepare);
        buildTextureResourceDelta(textureResources, texture_revisions_, forceFullPrepare, prepare);
    }
    if (prepare) {
        force_full_prepare_ = false;
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

void RenderWorldSync::rebuildEmpty(engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare) {
    last_stats_.reset();
    world.clear();
    if (prepare) {
        prepare->clear();
    }
    const bool forceFullPrepare = force_full_prepare_;
    buildGeometryResourceDelta({}, geometry_revisions_, forceFullPrepare, prepare);
    buildTextureResourceDelta({}, texture_revisions_, forceFullPrepare, prepare);
    if (prepare) {
        force_full_prepare_ = false;
    }
    referenced_asset_revisions_.clear();
    asset_change_cursor_ = {};
    *scene_state_ = {};
}

bool RenderWorldSync::referencedAssetsChanged(const asset::AssetLibrary& assets) const {
    if (force_full_prepare_) {
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
    geometry_revisions_.clear();
    texture_revisions_.clear();
    referenced_asset_revisions_.clear();
    asset_change_cursor_ = {};
    force_full_prepare_ = true;
    *scene_state_ = {};
}

}  // namespace mulan::view
