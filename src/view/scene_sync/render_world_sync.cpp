#include "scene_sync/render_world_sync.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/view/core/preview_layer.h>
#include <mulan/view/scene_sync/render_scene.h>
#include <mulan/view/scene_sync/scene_proxy.h>

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
using GeometryContentRevision = std::array<uint64_t, 4>;

struct GeometryResourceCandidate {
    const graphics::Mesh* mesh = nullptr;
    uint64_t contentDomain = 0;
    uint64_t sourceRevision = 0;
};

using GeometryResourceCandidateMap = std::unordered_map<engine::AssetGpuKey, GeometryResourceCandidate>;

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
    if (previous[0] != current[0]) {
        return true;
    }
    // Asset/Preview revision 用来触发 world rebuild；实际上传粒度继续精确到每个 mesh key。
    // 预览换源会改变 contentDomain，即使 generation 恰好相同也不会误复用。
    return previous[2] != current[2] || previous[3] != current[3];
}

void observeAsset(const asset::Asset* source, AssetRevisionMap& revisions) {
    if (source) {
        revisions.insert_or_assign(source->id(), source->revision());
    }
}

void collectGeometryResource(GeometryResourceCandidateMap& resources, engine::AssetGpuKey key,
                             const graphics::Mesh& mesh, uint64_t contentDomain, uint64_t sourceRevision) {
    if (!key || mesh.empty()) {
        return;
    }
    resources.try_emplace(key, GeometryResourceCandidate{
                                       .mesh = &mesh,
                                       .contentDomain = contentDomain,
                                       .sourceRevision = sourceRevision,
                               });
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
                                      bool (asset::MaterialAsset::*srgbGetter)() const, AssetRevisionMap& revisions) {
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
    observeAsset(texture, revisions);
    if (!texture->hasImage())
        return {};

    engine::RenderTextureDesc desc;
    desc.resourceKey = engine::makeAssetGpuKey(textureId.value);
    desc.image = texture->image();
    desc.contentRevision = texture->revision();
    desc.srgb = (material->*srgbGetter)();
    return desc;
}

engine::RenderMaterialDesc materialDesc(const asset::AssetLibrary& assets, asset::AssetId materialId,
                                        AssetRevisionMap& revisions) {
    engine::RenderMaterialDesc desc;
    // 无材质或材质资产失效时统一回退到稳定的内置身份，避免 world generation 污染缓存键。
    desc.resourceKey = engine::defaultRenderMaterialResourceKey();
    if (!materialId) {
        return desc;
    }

    const auto* material = dynamic_cast<const asset::MaterialAsset*>(assets.asset(materialId));
    if (!material) {
        return desc;
    }
    observeAsset(material, revisions);

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
                                        &asset::MaterialAsset::baseColorTextureSrgb, revisions);
    desc.normalTexture = textureDesc(assets, materialId, &asset::MaterialAsset::normalTexture,
                                     &asset::MaterialAsset::normalTextureSrgb, revisions);
    desc.metallicRoughnessTexture = textureDesc(assets, materialId, &asset::MaterialAsset::metallicRoughnessTexture,
                                                &asset::MaterialAsset::metallicRoughnessTextureSrgb, revisions);
    desc.emissiveTexture = textureDesc(assets, materialId, &asset::MaterialAsset::emissiveTexture,
                                       &asset::MaterialAsset::emissiveTextureSrgb, revisions);
    desc.ambientOcclusionTexture = textureDesc(assets, materialId, &asset::MaterialAsset::occlusionTexture,
                                               &asset::MaterialAsset::occlusionTextureSrgb, revisions);

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
                            GeometryResourceCandidateMap& resources, uint64_t previewSourceRevision,
                            RenderWorldSyncStats& stats, engine::RenderMaterialHandle toolMaterial,
                            engine::RenderMaterialHandle snapMaterial, engine::RenderMaterialHandle gripMaterial,
                            engine::RenderMaterialHandle gripHotMaterial) {
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
        // 预览没有 Asset 身份，以稳定角色槽位 key + PreviewLayer generation 作为内容版本。
        collectGeometryResource(resources, geometryDesc.resourceKey, mesh, previewSourceRevision, preview.generation());

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
                             engine::RenderWorld& world, AssetRevisionMap& revisions, RenderWorldSyncStats& stats,
                             std::unordered_map<uint64_t, engine::GeometryHandle>& geometryHandles,
                             engine::RenderMaterialHandle toolMaterial, engine::RenderMaterialHandle snapMaterial,
                             engine::RenderMaterialHandle gripMaterial, engine::RenderMaterialHandle gripHotMaterial) {
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
        observeAsset(geometry, revisions);

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
                // 引用几何已经由 SceneWorld 的可靠资源批次持有。OverlayWorld 只借用同一稳定 key，
                // 不能在预览结束时将仍被 SceneWorld 使用的资源误标记为退役。
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

void appendPreview(const RenderScene* scene, const asset::AssetLibrary* assets, const PreviewLayer* preview,
                   engine::RenderWorld& world, GeometryResourceCandidateMap& resources, AssetRevisionMap& revisions,
                   uint64_t previewSourceRevision, RenderWorldSyncStats& stats,
                   std::unordered_map<uint64_t, engine::GeometryHandle>& geometryHandles) {
    if (!preview || preview->empty()) {
        return;
    }

    const engine::RenderMaterialHandle toolMaterial = world.addMaterial(previewMaterialDesc(PreviewVisualRole::Tool));
    const engine::RenderMaterialHandle snapMaterial = world.addMaterial(previewMaterialDesc(PreviewVisualRole::Snap));
    const engine::RenderMaterialHandle gripMaterial = world.addMaterial(previewMaterialDesc(PreviewVisualRole::Grip));
    const engine::RenderMaterialHandle gripHotMaterial =
            world.addMaterial(previewMaterialDesc(PreviewVisualRole::GripHot));

    appendPreviewDrawables(*preview, world, resources, previewSourceRevision, stats, toolMaterial, snapMaterial,
                           gripMaterial, gripHotMaterial);
    if (scene && assets) {
        appendPreviewReferences(*preview, *scene, *assets, world, revisions, stats, geometryHandles, toolMaterial,
                                snapMaterial, gripMaterial, gripHotMaterial);
    }
}

void buildGeometryResourceDelta(const GeometryResourceCandidateMap& current,
                                std::unordered_map<engine::AssetGpuKey, GeometryContentRevision>& previous,
                                bool forceFullPrepare, engine::RenderResourcePrepareList* prepare) {
    if (!prepare) {
        return;
    }

    std::vector<engine::AssetGpuKey> retiredKeys;
    retiredKeys.reserve(previous.size());
    for (const auto& [key, revision] : previous) {
        (void) revision;
        if (!current.contains(key)) {
            retiredKeys.push_back(key);
        }
    }
    std::ranges::sort(retiredKeys, {}, &engine::AssetGpuKey::value);
    for (const engine::AssetGpuKey key : retiredKeys) {
        prepare->retireGeometry(key);
    }

    std::vector<engine::AssetGpuKey> currentKeys;
    currentKeys.reserve(current.size());
    for (const auto& [key, resource] : current) {
        (void) resource;
        currentKeys.push_back(key);
    }
    std::ranges::sort(currentKeys, {}, &engine::AssetGpuKey::value);

    std::unordered_map<engine::AssetGpuKey, GeometryContentRevision> next;
    next.reserve(current.size());
    for (const engine::AssetGpuKey key : currentKeys) {
        const GeometryResourceCandidate& resource = current.at(key);
        const auto previousIt = previous.find(key);
        const bool existed = previousIt != previous.end();
        GeometryContentRevision currentRevision{ resource.contentDomain, resource.sourceRevision, 0, 0 };
        if (existed && previousIt->second[0] == resource.contentDomain &&
            previousIt->second[1] == resource.sourceRevision) {
            // transform/selection/material 等 world-only rebuild 不会扫描大网格字节；
            // 只有新 key 或对应内容源版本变化时才重算该 key 的指纹。
            currentRevision = previousIt->second;
        } else {
            const auto [primary, secondary] = meshContentFingerprint(*resource.mesh);
            currentRevision[2] = primary;
            currentRevision[3] = secondary;
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
    if (lhs.resourceKey.value != rhs.resourceKey.value) {
        return lhs.resourceKey.value < rhs.resourceKey.value;
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
                                   engine::RenderWorld& world, engine::RenderResourcePrepareList* prepare) {
    last_stats_.reset();
    if (prepare) {
        prepare->clear();
    }

    SceneState& state = *scene_state_;
    const RenderSceneChangeSet changes = scene.readChanges(state.cursor);
    const bool fullRebuild = changes.requiresFullResync();
    last_stats_.fullRebuild = fullRebuild;
    if (fullRebuild) {
        world.clear();
        state = {};
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

    auto removeDependencyLinks = [&](scene::EntityId entity, const SceneState::ObjectEntry& entry) {
        for (asset::AssetId dependency : entry.dependencies) {
            auto users = state.assetUsers.find(dependency);
            if (users == state.assetUsers.end()) {
                continue;
            }
            users->second.erase(entity);
            if (users->second.empty()) {
                state.assetUsers.erase(users);
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
            }
        }
        for (uint64_t key : entry.materialKeys) {
            auto material = state.materials.find(key);
            if (material != state.materials.end() && --material->second.referenceCount == 0) {
                world.removeMaterial(material->second.handle);
                state.materials.erase(material);
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
        pending.object.worldBounds = proxy->worldBounds;
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
            geometryDesc.resourceKey = engine::makeAssetGpuKey(item.geometryKey);
            geometryDesc.topology = mesh.topology;
            geometryDesc.vertexLayout = mesh.layout;
            geometryDesc.empty = mesh.empty();
            pending.geometries.insert_or_assign(item.geometryKey, geometryDesc);
            pending.geometryResources.insert_or_assign(
                    item.geometryKey, GeometryResourceCandidate{ .mesh = &mesh,
                                                                 .contentDomain = 0,
                                                                 .sourceRevision = geometry->revision() });

            const uint64_t materialKey = item.material.value;
            if (!pending.materials.contains(materialKey)) {
                AssetRevisionMap materialDependencies;
                engine::RenderMaterialDesc desc = materialDesc(assets, item.material, materialDependencies);
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
                }
            }
            removeDependencyLinks(entity, previous->second);
        }
        nextEntry.dependencies = std::move(pending.dependencies);
        for (asset::AssetId dependency : nextEntry.dependencies) {
            state.assetUsers[dependency].insert(entity);
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

    std::unordered_set<scene::EntityId> entitiesToPatch;
    if (fullRebuild) {
        scene.forEachProxy([&](const SceneProxy& proxy) { entitiesToPatch.insert(proxy.entity); });
    } else {
        for (const RenderSceneChange& change : changes.changes) {
            entitiesToPatch.insert(change.entity);
        }
        for (const auto& [assetId, revision] : referenced_asset_revisions_) {
            const asset::Asset* current = assets.asset(assetId);
            if (current && current->revision() == revision) {
                continue;
            }
            if (const auto users = state.assetUsers.find(assetId); users != state.assetUsers.end()) {
                entitiesToPatch.insert(users->second.begin(), users->second.end());
            }
        }
    }
    for (scene::EntityId entity : entitiesToPatch) {
        patchEntity(entity);
    }

    GeometryResourceCandidateMap geometryResources;
    for (const auto& [key, entry] : state.geometries) {
        geometryResources.emplace(engine::makeAssetGpuKey(key), entry.resource);
    }
    TextureResourceCandidateMap textureResources;
    for (const auto& [key, entry] : state.materials) {
        (void) key;
        collectMaterialTextureResources(textureResources, entry.desc);
    }
    AssetRevisionMap referencedAssetRevisions;
    for (const auto& [assetId, users] : state.assetUsers) {
        (void) users;
        observeAsset(assets.asset(assetId), referencedAssetRevisions);
    }

    const bool forceFullPrepare = force_full_prepare_;
    buildGeometryResourceDelta(geometryResources, geometry_revisions_, forceFullPrepare, prepare);
    buildTextureResourceDelta(textureResources, texture_revisions_, forceFullPrepare, prepare);
    if (prepare) {
        force_full_prepare_ = false;
    }
    referenced_asset_revisions_ = std::move(referencedAssetRevisions);
    state.cursor = scene.currentChangeCursor();
    last_stats_.sceneProxyCount = scene.proxyCount();
    last_stats_.missingGeometryAssetCount = scene.lastSyncStats().missingGeometryCount;
    last_stats_.sceneObjectCount = state.objects.size();
    last_stats_.worldObjectCount = world.objectCount();
    last_stats_.worldGeometryCount = world.geometryCount();
    last_stats_.worldMaterialCount = world.materialCount();
}

void RenderWorldSync::rebuildOverlay(const RenderScene* scene, const asset::AssetLibrary* assets,
                                     const PreviewLayer* preview, engine::RenderWorld& world,
                                     engine::RenderResourcePrepareList* prepare) {
    last_stats_.reset();
    world.clear();
    if (prepare) {
        prepare->clear();
    }

    std::unordered_map<uint64_t, engine::GeometryHandle> geometryHandles;
    GeometryResourceCandidateMap geometryResources;
    AssetRevisionMap referencedAssetRevisions;
    appendPreview(scene, assets, preview, world, geometryResources, referencedAssetRevisions, preview_source_revision_,
                  last_stats_, geometryHandles);

    const bool forceFullPrepare = force_full_prepare_;
    buildGeometryResourceDelta(geometryResources, geometry_revisions_, forceFullPrepare, prepare);
    buildTextureResourceDelta({}, texture_revisions_, forceFullPrepare, prepare);
    if (prepare) {
        force_full_prepare_ = false;
    }
    referenced_asset_revisions_ = std::move(referencedAssetRevisions);
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
    *scene_state_ = {};
}

bool RenderWorldSync::referencedAssetsChanged(const asset::AssetLibrary& assets) const {
    if (force_full_prepare_) {
        return true;
    }
    for (const auto& [id, revision] : referenced_asset_revisions_) {
        const asset::Asset* current = assets.asset(id);
        if (!current || current->revision() != revision) {
            return true;
        }
    }
    return false;
}

void RenderWorldSync::reset() {
    last_stats_.reset();
    geometry_revisions_.clear();
    texture_revisions_.clear();
    referenced_asset_revisions_.clear();
    preview_source_revision_ = 1;
    force_full_prepare_ = true;
    *scene_state_ = {};
}

void RenderWorldSync::invalidatePreviewResources() {
    ++preview_source_revision_;
    if (preview_source_revision_ == 0) {
        preview_source_revision_ = 1;
    }
}

}  // namespace mulan::view
