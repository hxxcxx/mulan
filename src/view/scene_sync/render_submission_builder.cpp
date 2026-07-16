/**
 * @file render_submission_builder.cpp
 * @brief RenderSubmissionBuilder 实现。
 * @author hxxcxx
 * @date 2026-07-10
 */

#include "scene_sync/render_submission_builder.h"

#include <mulan/view/core/preview_layer.h>
#include "scene_sync/render_item_builder.h"
#include <mulan/view/scene_sync/render_scene.h>
#include <mulan/view/scene_sync/scene_proxy.h>

#include <mulan/asset/asset_library.h>
#include <mulan/asset/geometry_asset.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mulan::view {
namespace {

uint64_t sceneChangeDomain(const RenderScene* scene) {
    return scene ? scene->currentChangeCursor().domain : 0;
}

constexpr std::array kPreviewRoles{ PreviewVisualRole::Tool, PreviewVisualRole::Snap, PreviewVisualRole::Grip,
                                    PreviewVisualRole::GripHot };

struct OverlayDirectGeometryState {
    engine::RenderResourceKey key;
    engine::GeometryHandle handle;
    uint64_t primaryFingerprint = 0;
    uint64_t secondaryFingerprint = 0;
};

struct OverlayReferenceGeometryState {
    engine::GeometryHandle handle;
    engine::RenderGeometryDesc desc;
};

struct OverlayRoleState {
    uint64_t geometryGeneration = 0;
    uint64_t referenceGeneration = 0;
    engine::RenderMaterialHandle material;
    engine::RenderObjectId directObject;
    std::vector<OverlayDirectGeometryState> directGeometries;
    std::vector<engine::RenderObjectId> referenceObjects;
    std::unordered_map<engine::RenderResourceKey, OverlayReferenceGeometryState> referenceGeometries;
    std::unordered_map<asset::AssetId, uint64_t> referencedAssetRevisions;
    RenderItemDiagnostics directDiagnostics;
    RenderItemDiagnostics referenceDiagnostics;
};

struct PendingReferenceDrawable {
    engine::RenderResourceKey geometryKey;
    engine::RenderBucket bucket = engine::RenderBucket::OverlaySurface;
    size_t sourceDrawableIndex = 0;
};

struct PendingReferenceObject {
    engine::RenderObjectDesc desc;
    std::vector<PendingReferenceDrawable> drawables;
};

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
    const auto mixValue = [&](uint64_t value) {
        hashValue(primary, value);
        hashValue(secondary, value ^ 0x9e3779b97f4a7c15ull);
    };
    const auto mixBytes = [&](std::span<const std::byte> bytes) {
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

bool sameVertexLayout(const graphics::VertexLayout& lhs, const graphics::VertexLayout& rhs) {
    if (lhs.stride() != rhs.stride() || lhs.attrCount() != rhs.attrCount() || lhs.bufferCount() != rhs.bufferCount()) {
        return false;
    }
    for (uint8_t index = 0; index < lhs.attrCount(); ++index) {
        if (!(lhs[index] == rhs[index])) {
            return false;
        }
    }
    return true;
}

bool sameGeometryDesc(const engine::RenderGeometryDesc& lhs, const engine::RenderGeometryDesc& rhs) {
    return lhs.resourceKey == rhs.resourceKey && lhs.topology == rhs.topology && lhs.empty == rhs.empty &&
           sameVertexLayout(lhs.vertexLayout, rhs.vertexLayout);
}

bool resourceKeyLess(const engine::RenderResourceKey& lhs, const engine::RenderResourceKey& rhs) {
    if (lhs.domain.value != rhs.domain.value)
        return lhs.domain.value < rhs.domain.value;
    if (lhs.source != rhs.source)
        return lhs.source < rhs.source;
    if (lhs.subresource != rhs.subresource)
        return lhs.subresource < rhs.subresource;
    return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
}

void accumulate(RenderItemDiagnostics& destination, const RenderItemDiagnostics& source) {
    destination.accepted += source.accepted;
    destination.rejectedEmpty += source.rejectedEmpty;
    destination.rejectedTopology += source.rejectedTopology;
    destination.rejectedLayout += source.rejectedLayout;
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

bool roleHasReferenceInput(const PreviewLayer* preview, PreviewVisualRole role) {
    if (!preview) {
        return false;
    }
    return std::ranges::any_of(preview->references(),
                               [role](const PreviewReference& reference) { return reference.role == role; });
}

bool roleHasReferenceState(const PreviewLayer* preview, PreviewVisualRole role, const OverlayRoleState& state) {
    return roleHasReferenceInput(preview, role) || !state.referenceObjects.empty() ||
           !state.referenceGeometries.empty() || !state.referencedAssetRevisions.empty();
}

engine::RenderMaterialHandle ensureRoleMaterial(engine::RenderWorld& world, OverlayRoleState& state,
                                                PreviewVisualRole role, engine::ResourceDomainId previewDomain) {
    if (!state.material) {
        state.material = world.addMaterial(previewMaterialDesc(role, previewDomain));
    }
    return state.material;
}

bool overlayCacheValid(const engine::RenderWorld& world, const OverlayIncrementalState& state);

}  // namespace

struct OverlayIncrementalState {
    std::array<OverlayRoleState, kPreviewVisualRoleCount> roles;
    asset::AssetChangeCursor assetChangeCursor;
    bool forceFullPrepare = true;
};

namespace {

bool overlayCacheValid(const engine::RenderWorld& world, const OverlayIncrementalState& state) {
    const engine::RenderWorldSnapshot snapshot = world.snapshot();
    for (const OverlayRoleState& role : state.roles) {
        if (role.material && !snapshot.material(role.material)) {
            return false;
        }
        if (role.directObject && !snapshot.object(role.directObject)) {
            return false;
        }
        for (const OverlayDirectGeometryState& geometry : role.directGeometries) {
            if (!geometry.handle || !snapshot.geometry(geometry.handle)) {
                return false;
            }
        }
        for (engine::RenderObjectId object : role.referenceObjects) {
            if (!object || !snapshot.object(object)) {
                return false;
            }
        }
        for (const auto& [_, geometry] : role.referenceGeometries) {
            if (!geometry.handle || !snapshot.geometry(geometry.handle)) {
                return false;
            }
        }
    }
    return true;
}

void invalidateOverlayHandles(OverlayIncrementalState& state) {
    for (OverlayRoleState& role : state.roles) {
        role.material = {};
        role.directObject = {};
        for (OverlayDirectGeometryState& geometry : role.directGeometries) {
            geometry.handle = {};
        }
        role.referenceObjects.clear();
        role.referenceGeometries.clear();
    }
}

bool patchDirectRole(const PreviewLayer* preview, PreviewVisualRole role, engine::ResourceDomainId previewDomain,
                     engine::RenderWorld& world, OverlayRoleState& state, bool forceFullPrepare,
                     engine::RenderResourcePrepareList& prepare, RenderWorldSyncStats& stats) {
    const std::span<const PreviewDrawable> drawables =
            preview ? preview->drawables(role) : std::span<const PreviewDrawable>{};
    std::vector<RenderItem> items;
    RenderItemDiagnostics diagnostics;
    RenderItemBuilder::buildPreviewItems(drawables, items, &diagnostics);
    state.directDiagnostics = diagnostics;

    std::unordered_map<engine::RenderResourceKey, const OverlayDirectGeometryState*> previousByKey;
    previousByKey.reserve(state.directGeometries.size());
    for (const OverlayDirectGeometryState& geometry : state.directGeometries) {
        previousByKey.emplace(geometry.key, &geometry);
    }

    std::vector<OverlayDirectGeometryState> nextGeometries;
    nextGeometries.reserve(items.size());
    engine::RenderObjectDesc object;
    object.pickId = engine::PickId::invalid();
    object.worldTransform = math::Mat4(1.0);
    object.worldBounds = math::AABB3::empty();
    object.visible = true;
    object.selected = false;

    if (!items.empty()) {
        ensureRoleMaterial(world, state, role, previewDomain);
    }
    for (const RenderItem& item : items) {
        const graphics::Mesh& mesh = *item.mesh;
        const engine::RenderResourceKey key = engine::makeRenderResourceKey(
                previewDomain, item.geometryKey, engine::RenderResourceKind::PreviewGeometry);
        engine::RenderGeometryDesc desc;
        desc.resourceKey = key;
        desc.topology = mesh.topology;
        desc.vertexLayout = mesh.layout;
        desc.empty = mesh.empty();

        const auto [primary, secondary] = meshContentFingerprint(mesh);
        const auto previous = previousByKey.find(key);
        const bool existed = previous != previousByKey.end();
        const bool contentChanged = !existed || previous->second->primaryFingerprint != primary ||
                                    previous->second->secondaryFingerprint != secondary;
        engine::GeometryHandle handle = existed ? previous->second->handle : engine::GeometryHandle{};
        if (!handle) {
            handle = world.addGeometry(desc);
        } else if (contentChanged && !world.updateGeometry(handle, desc)) {
            return false;
        }

        if (forceFullPrepare || contentChanged) {
            prepare.addGeometry(key, mesh, forceFullPrepare || existed);
        }
        nextGeometries.push_back(OverlayDirectGeometryState{
                .key = key,
                .handle = handle,
                .primaryFingerprint = primary,
                .secondaryFingerprint = secondary,
        });
        if (!mesh.bounds.isEmpty()) {
            object.worldBounds.expand(mesh.bounds);
        }
        object.drawables.push_back(engine::RenderObjectDrawable{
                .geometry = handle,
                .material = state.material,
                .bucket = item.bucket,
                .sourceDrawableIndex = item.sourceDrawableIndex,
        });
    }

    if (object.drawables.empty()) {
        if (state.directObject) {
            if (!world.removeObject(state.directObject)) {
                return false;
            }
            state.directObject = {};
            ++stats.removedObjectCount;
            ++stats.patchedObjectCount;
        }
    } else if (state.directObject) {
        if (!world.updateObject(state.directObject, std::move(object))) {
            return false;
        }
        ++stats.updatedObjectCount;
        ++stats.patchedObjectCount;
    } else {
        state.directObject = world.addObject(std::move(object));
        ++stats.addedObjectCount;
        ++stats.patchedObjectCount;
    }

    std::unordered_set<engine::RenderResourceKey> nextKeys;
    nextKeys.reserve(nextGeometries.size());
    for (const OverlayDirectGeometryState& geometry : nextGeometries) {
        nextKeys.insert(geometry.key);
    }
    for (const OverlayDirectGeometryState& previous : state.directGeometries) {
        if (nextKeys.contains(previous.key)) {
            continue;
        }
        if (previous.handle && !world.removeGeometry(previous.handle)) {
            return false;
        }
        prepare.retireGeometry(previous.key);
    }
    state.directGeometries = std::move(nextGeometries);
    return true;
}

bool patchReferenceRole(const PreviewLayer* preview, const RenderScene* scene, const asset::AssetLibrary* assets,
                        engine::ResourceDomainId assetDomain, engine::ResourceDomainId previewDomain,
                        PreviewVisualRole role, engine::RenderWorld& world, OverlayRoleState& state,
                        RenderWorldSyncStats& stats) {
    std::vector<PendingReferenceObject> pendingObjects;
    std::unordered_map<engine::RenderResourceKey, engine::RenderGeometryDesc> pendingGeometries;
    std::unordered_map<asset::AssetId, uint64_t> referencedAssetRevisions;
    RenderItemDiagnostics combinedDiagnostics;

    if (preview && scene && assets) {
        std::vector<asset::Drawable> sourceDrawables;
        std::vector<RenderItem> renderItems;
        for (const PreviewReference& reference : preview->references()) {
            if (reference.role != role || !reference.valid()) {
                continue;
            }
            const SceneProxy* proxy = scene->proxy(reference.entity);
            if (!proxy || !proxy->visible || !proxy->geometry) {
                continue;
            }
            const auto* geometry = dynamic_cast<const asset::GeometryAsset*>(assets->asset(proxy->geometry));
            if (!geometry) {
                ++stats.missingGeometryAssetCount;
                continue;
            }
            referencedAssetRevisions.insert_or_assign(geometry->id(), geometry->revision());

            sourceDrawables.clear();
            geometry->collectDrawables(sourceDrawables);
            RenderItemDiagnostics diagnostics;
            RenderItemBuilder::buildSceneItems(
                    proxy->geometry, std::span<const asset::Drawable>{ sourceDrawables.data(), sourceDrawables.size() },
                    renderItems, &diagnostics);
            accumulate(combinedDiagnostics, diagnostics);

            PendingReferenceObject pending;
            pending.desc.pickId = engine::PickId::invalid();
            pending.desc.worldTransform =
                    reference.overrideWorldTransform ? reference.worldTransform : proxy->worldTransform;
            pending.desc.worldBounds = math::AABB3::empty();
            pending.desc.visible = reference.visible;
            pending.desc.selected = false;
            for (const RenderItem& item : renderItems) {
                const std::optional<engine::RenderBucket> bucket = overlayBucketForReference(item.bucket);
                if (!bucket) {
                    continue;
                }
                const graphics::Mesh& mesh = *item.mesh;
                const engine::RenderResourceKey key = engine::makeRenderResourceKey(
                        assetDomain, proxy->geometry.value, engine::RenderResourceKind::Geometry,
                        static_cast<uint32_t>(item.sourceDrawableIndex));
                engine::RenderGeometryDesc desc;
                desc.resourceKey = key;
                desc.topology = mesh.topology;
                desc.vertexLayout = mesh.layout;
                desc.empty = mesh.empty();
                pendingGeometries.try_emplace(key, std::move(desc));
                if (!mesh.bounds.isEmpty()) {
                    pending.desc.worldBounds.expand(mesh.bounds.transformed(pending.desc.worldTransform));
                }
                pending.drawables.push_back(PendingReferenceDrawable{
                        .geometryKey = key,
                        .bucket = *bucket,
                        .sourceDrawableIndex = item.sourceDrawableIndex,
                });
            }
            if (!pending.drawables.empty()) {
                pendingObjects.push_back(std::move(pending));
            }
        }
    }
    state.referenceDiagnostics = combinedDiagnostics;

    std::vector<engine::RenderResourceKey> orderedKeys;
    orderedKeys.reserve(pendingGeometries.size());
    for (const auto& [key, _] : pendingGeometries) {
        orderedKeys.push_back(key);
    }
    std::sort(orderedKeys.begin(), orderedKeys.end(), resourceKeyLess);

    std::unordered_map<engine::RenderResourceKey, OverlayReferenceGeometryState> nextGeometries;
    nextGeometries.reserve(pendingGeometries.size());
    for (engine::RenderResourceKey key : orderedKeys) {
        const engine::RenderGeometryDesc& desc = pendingGeometries.at(key);
        auto previous = state.referenceGeometries.find(key);
        engine::GeometryHandle handle =
                previous != state.referenceGeometries.end() ? previous->second.handle : engine::GeometryHandle{};
        if (!handle) {
            handle = world.addGeometry(desc);
        } else if (!sameGeometryDesc(previous->second.desc, desc) && !world.updateGeometry(handle, desc)) {
            return false;
        }
        nextGeometries.emplace(key, OverlayReferenceGeometryState{ .handle = handle, .desc = desc });
    }

    if (!pendingObjects.empty()) {
        ensureRoleMaterial(world, state, role, previewDomain);
    }
    std::vector<engine::RenderObjectId> nextObjects;
    nextObjects.reserve(pendingObjects.size());
    for (size_t objectIndex = 0; objectIndex < pendingObjects.size(); ++objectIndex) {
        PendingReferenceObject& pending = pendingObjects[objectIndex];
        for (const PendingReferenceDrawable& drawable : pending.drawables) {
            const auto geometry = nextGeometries.find(drawable.geometryKey);
            if (geometry == nextGeometries.end()) {
                return false;
            }
            pending.desc.drawables.push_back(engine::RenderObjectDrawable{
                    .geometry = geometry->second.handle,
                    .material = state.material,
                    .bucket = drawable.bucket,
                    .sourceDrawableIndex = drawable.sourceDrawableIndex,
            });
        }
        if (objectIndex < state.referenceObjects.size()) {
            const engine::RenderObjectId id = state.referenceObjects[objectIndex];
            if (!world.updateObject(id, std::move(pending.desc))) {
                return false;
            }
            nextObjects.push_back(id);
            ++stats.updatedObjectCount;
        } else {
            nextObjects.push_back(world.addObject(std::move(pending.desc)));
            ++stats.addedObjectCount;
        }
        ++stats.patchedObjectCount;
    }
    for (size_t objectIndex = pendingObjects.size(); objectIndex < state.referenceObjects.size(); ++objectIndex) {
        if (!world.removeObject(state.referenceObjects[objectIndex])) {
            return false;
        }
        ++stats.removedObjectCount;
        ++stats.patchedObjectCount;
    }

    for (const auto& [key, previous] : state.referenceGeometries) {
        if (!nextGeometries.contains(key) && previous.handle && !world.removeGeometry(previous.handle)) {
            return false;
        }
    }
    state.referenceObjects = std::move(nextObjects);
    state.referenceGeometries = std::move(nextGeometries);
    state.referencedAssetRevisions = std::move(referencedAssetRevisions);
    return true;
}

bool hasAnyReferenceState(const PreviewLayer* preview, const OverlayIncrementalState& state) {
    for (const PreviewVisualRole role : kPreviewRoles) {
        if (roleHasReferenceState(preview, role, state.roles[previewVisualRoleIndex(role)])) {
            return true;
        }
    }
    return false;
}

bool assetChangesAffectReferences(const asset::AssetLibrary* assets, const PreviewLayer* preview,
                                  OverlayIncrementalState& state) {
    if (!assets) {
        return false;
    }
    const asset::AssetChangeSet changes = assets->readChanges(state.assetChangeCursor);
    if (changes.requiresFullResync()) {
        if (hasAnyReferenceState(preview, state)) {
            return true;
        }
        state.assetChangeCursor = changes.cursorAfterApply();
        return false;
    }
    for (const asset::AssetChange& change : changes.changes) {
        if (!change.asset) {
            if (hasAnyReferenceState(preview, state)) {
                return true;
            }
            continue;
        }
        for (const OverlayRoleState& role : state.roles) {
            if (role.referencedAssetRevisions.contains(change.asset)) {
                return true;
            }
        }
    }
    // Overlay 是 Asset journal 的独立消费者。全部事件均与预览引用无关时可在检查点
    // 直接确认游标；真正相关的批次留给 rebuildOverlay 成功发布后再统一确认。
    state.assetChangeCursor = changes.cursorAfterApply();
    return false;
}

void markAssetChangedRoles(const asset::AssetChangeSet& changes, const PreviewLayer* preview,
                           const OverlayIncrementalState& state,
                           std::array<bool, kPreviewVisualRoleCount>& referenceDirty) {
    if (changes.requiresFullResync()) {
        for (size_t index = 0; index < kPreviewVisualRoleCount; ++index) {
            referenceDirty[index] =
                    referenceDirty[index] || roleHasReferenceState(preview, kPreviewRoles[index], state.roles[index]);
        }
        return;
    }
    for (const asset::AssetChange& change : changes.changes) {
        for (size_t index = 0; index < kPreviewVisualRoleCount; ++index) {
            if ((!change.asset && roleHasReferenceState(preview, kPreviewRoles[index], state.roles[index])) ||
                (change.asset && state.roles[index].referencedAssetRevisions.contains(change.asset))) {
                referenceDirty[index] = true;
            }
        }
    }
}

}  // namespace

RenderSubmissionBuilder::RenderSubmissionBuilder()
    : overlay_state_(std::make_unique<OverlayIncrementalState>()),
      preview_resource_domain_(engine::allocateTransientResourceDomain()) {
}

RenderSubmissionBuilder::~RenderSubmissionBuilder() = default;

void RenderSubmissionBuilder::reset() {
    scene_ = nullptr;
    assets_ = nullptr;
    asset_resource_domain_ = {};
    preview_ = nullptr;
    last_scene_generation_ = 0;
    bound_scene_change_domain_ = 0;
    last_scene_change_domain_ = 0;
    last_overlay_scene_change_domain_ = 0;
    last_preview_generation_ = 0;
    last_overlay_scene_generation_ = 0;
    submission_generation_ = 0;
    scene_source_dirty_ = true;
    preview_source_dirty_ = true;
    overlay_reference_source_dirty_ = true;
    scene_world_.clear();
    overlay_world_.clear();
    scene_world_snapshot_.reset();
    overlay_world_snapshot_.reset();
    last_scene_sync_stats_ = {};
    last_overlay_sync_stats_ = {};
    diagnostics_ = {};
    light_environment_ = {};
    pending_prepare_.clear();
    resource_batch_id_ = 0;
    scene_world_sync_.reset();
    overlay_state_ = std::make_unique<OverlayIncrementalState>();
}

void RenderSubmissionBuilder::setScene(const RenderScene* scene, const asset::AssetLibrary* assets) {
    const uint64_t nextSceneDomain = sceneChangeDomain(scene);
    const engine::ResourceDomainId nextAssetDomain =
            assets ? engine::resourceDomainForAssetLibrary(assets->domainId()) : engine::ResourceDomainId{};
    if (scene_ == scene && assets_ == assets && bound_scene_change_domain_ == nextSceneDomain &&
        asset_resource_domain_ == nextAssetDomain) {
        return;
    }

    // 裸指针不能作为长期身份：对象析构后 placement-new/optional::emplace 可能复用同址。
    // GPU key 与增量快照都必须以不可复用的 domain 识别来源换代。
    const bool assetDomainChanged = asset_resource_domain_ != nextAssetDomain;
    scene_ = scene;
    assets_ = assets;
    bound_scene_change_domain_ = nextSceneDomain;
    asset_resource_domain_ = nextAssetDomain;
    if (assetDomainChanged) {
        invalidateResources();
    } else {
        // 同一资产域内切换 scene 时保留 key/revision 基线，仅对真实差量上传或退役。
        scene_source_dirty_ = true;
        // 直接预览几何与 Scene 无关；换 Scene 只重投影各角色的 PreviewReference。
        overlay_reference_source_dirty_ = true;
    }
}

void RenderSubmissionBuilder::setPreviewLayer(const PreviewLayer* preview) {
    if (preview_ == preview) {
        return;
    }

    preview_ = preview;
    // 换源后四个角色分别与现有稳定槽位做差量；相同内容继续复用 GPU key。
    preview_source_dirty_ = true;
}

void RenderSubmissionBuilder::setLightEnvironment(const engine::LightEnvironment& lightEnvironment) {
    light_environment_ = lightEnvironment;
}

RenderSubmission RenderSubmissionBuilder::build(const ViewState& viewState) {
    RenderSubmission submission;
    submission.view = viewState;
    submission.lightEnvironment = light_environment_;
    submission.sceneGeneration = scene_ ? scene_->generation() : 0;
    submission.geometryGeneration = scene_ ? scene_->geometryGeneration() : 0;
    submission.previewGeneration = preview_ ? preview_->generation() : 0;

    submission.rebuiltSceneWorld = needsSceneRebuild();
    submission.rebuiltOverlayWorld = needsOverlayRebuild();
    submission.rebuiltWorld = submission.rebuiltSceneWorld || submission.rebuiltOverlayWorld;
    if (submission.rebuiltSceneWorld) {
        rebuildScene(submission);
    }
    if (submission.rebuiltOverlayWorld) {
        rebuildOverlay(submission);
    }

    if (!submission.prepare.empty()) {
        pending_prepare_.merge(submission.prepare);
        advanceResourceBatch();
    }
    submission.prepare = pending_prepare_;
    submission.resourceBatchId = pending_prepare_.empty() ? 0 : resource_batch_id_;

    submission.sceneWorld = scene_world_snapshot_;
    submission.overlayWorld = overlay_world_snapshot_;
    submission.sceneSyncStats = last_scene_sync_stats_;
    submission.overlaySyncStats = last_overlay_sync_stats_;
    submission.generation = ++submission_generation_;
    if (submission_generation_ == 0) {
        submission_generation_ = 1;
        submission.generation = submission_generation_;
    }
    ++diagnostics_.submissionCount;
    if (submission.rebuiltWorld) {
        ++diagnostics_.worldRebuildCount;
    } else {
        ++diagnostics_.worldReuseCount;
    }
    if (submission.rebuiltSceneWorld) {
        ++diagnostics_.sceneWorldRebuildCount;
    } else {
        ++diagnostics_.sceneWorldReuseCount;
    }
    if (submission.rebuiltOverlayWorld) {
        ++diagnostics_.overlayWorldRebuildCount;
    } else {
        ++diagnostics_.overlayWorldReuseCount;
    }
    diagnostics_.lastResourceUpdateCount = submission.prepare.size();
    diagnostics_.lastSceneGeneration = submission.sceneGeneration;
    diagnostics_.lastGeometryGeneration = submission.geometryGeneration;
    diagnostics_.lastPreviewGeneration = submission.previewGeneration;
    return submission;
}

void RenderSubmissionBuilder::acknowledgeResources(uint64_t batchId) {
    if (batchId == 0 || batchId != resource_batch_id_) {
        return;
    }
    pending_prepare_.clear();
}

void RenderSubmissionBuilder::invalidateResources() {
    pending_prepare_.clear();
    advanceResourceBatch();
    scene_world_sync_.invalidateResources();
    scene_source_dirty_ = true;
    overlay_reference_source_dirty_ = true;
    overlay_state_->forceFullPrepare = true;
}

bool RenderSubmissionBuilder::needsSceneRebuild() const {
    if (scene_source_dirty_) {
        return true;
    }

    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    if (sceneGeneration != last_scene_generation_ || sceneChangeDomain(scene_) != last_scene_change_domain_) {
        return true;
    }
    return scene_ && assets_ && scene_world_sync_.referencedAssetsChanged(*assets_);
}

bool RenderSubmissionBuilder::needsOverlayRebuild() const {
    if (preview_source_dirty_ || overlay_reference_source_dirty_ || overlay_state_->forceFullPrepare) {
        return true;
    }

    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    if (previewGeneration != last_preview_generation_) {
        return true;
    }
    for (const PreviewVisualRole role : kPreviewRoles) {
        const OverlayRoleState& state = overlay_state_->roles[previewVisualRoleIndex(role)];
        const uint64_t geometryGeneration = preview_ ? preview_->geometryGeneration(role) : 0;
        const uint64_t referenceGeneration = preview_ ? preview_->referenceGeneration(role) : 0;
        if (geometryGeneration != state.geometryGeneration || referenceGeneration != state.referenceGeneration) {
            return true;
        }
    }

    // 只有引用场景实体的角色才依赖 SceneProxy generation；直接预览几何完全独立。
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    if (sceneGeneration != last_overlay_scene_generation_ ||
        sceneChangeDomain(scene_) != last_overlay_scene_change_domain_) {
        for (const PreviewVisualRole role : kPreviewRoles) {
            const OverlayRoleState& state = overlay_state_->roles[previewVisualRoleIndex(role)];
            if (roleHasReferenceState(preview_, role, state)) {
                return true;
            }
        }
    }
    return assetChangesAffectReferences(assets_, preview_, *overlay_state_);
}

void RenderSubmissionBuilder::rebuildScene(RenderSubmission& submission) {
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    engine::RenderResourcePrepareList prepare;

    if (!scene_ || !assets_) {
        scene_world_sync_.rebuildEmpty(scene_world_, &prepare);
        scene_world_snapshot_.reset();
        last_scene_sync_stats_ = scene_world_sync_.lastStats();
    } else {
        scene_world_sync_.rebuildScene(*scene_, *assets_, asset_resource_domain_, scene_world_, &prepare);
        scene_world_snapshot_ = std::make_shared<engine::RenderWorldSnapshot>(scene_world_.snapshot());
        last_scene_sync_stats_ = scene_world_sync_.lastStats();
    }
    submission.prepare.merge(prepare);

    last_scene_generation_ = sceneGeneration;
    last_scene_change_domain_ = sceneChangeDomain(scene_);
    scene_source_dirty_ = false;
}

void RenderSubmissionBuilder::rebuildOverlay(RenderSubmission& submission) {
    const uint64_t sceneGeneration = scene_ ? scene_->generation() : 0;
    const uint64_t previewGeneration = preview_ ? preview_->generation() : 0;
    engine::RenderResourcePrepareList prepare;

    std::array<bool, kPreviewVisualRoleCount> geometryDirty{};
    std::array<bool, kPreviewVisualRoleCount> referenceDirty{};
    bool recovered = !overlayCacheValid(overlay_world_, *overlay_state_);
    for (const PreviewVisualRole role : kPreviewRoles) {
        const size_t index = previewVisualRoleIndex(role);
        const uint64_t geometryGeneration = preview_ ? preview_->geometryGeneration(role) : 0;
        const uint64_t referenceGeneration = preview_ ? preview_->referenceGeneration(role) : 0;
        if (!preview_source_dirty_ &&
            ((overlay_state_->roles[index].geometryGeneration != 0 && geometryGeneration != 0 &&
              geometryGeneration < overlay_state_->roles[index].geometryGeneration) ||
             (overlay_state_->roles[index].referenceGeneration != 0 && referenceGeneration != 0 &&
              referenceGeneration < overlay_state_->roles[index].referenceGeneration))) {
            // 版本倒退表示换代/回绕或缓存基线异常，不能用普通差量解释。
            recovered = true;
        }
    }
    if (recovered) {
        overlay_world_.clear();
        invalidateOverlayHandles(*overlay_state_);
        geometryDirty.fill(true);
        referenceDirty.fill(true);
    }

    asset::AssetChangeSet assetChanges;
    if (assets_) {
        assetChanges = assets_->readChanges(overlay_state_->assetChangeCursor);
        markAssetChangedRoles(assetChanges, preview_, *overlay_state_, referenceDirty);
    }

    for (const PreviewVisualRole role : kPreviewRoles) {
        const size_t index = previewVisualRoleIndex(role);
        const OverlayRoleState& state = overlay_state_->roles[index];
        const uint64_t geometryGeneration = preview_ ? preview_->geometryGeneration(role) : 0;
        const uint64_t referenceGeneration = preview_ ? preview_->referenceGeneration(role) : 0;
        geometryDirty[index] = geometryDirty[index] || preview_source_dirty_ || overlay_state_->forceFullPrepare ||
                               geometryGeneration != state.geometryGeneration;
        referenceDirty[index] =
                referenceDirty[index] || preview_source_dirty_ || referenceGeneration != state.referenceGeneration;
        if (overlay_reference_source_dirty_ || sceneGeneration != last_overlay_scene_generation_) {
            referenceDirty[index] = referenceDirty[index] || roleHasReferenceState(preview_, role, state);
        }
    }

    last_overlay_sync_stats_.reset();
    last_overlay_sync_stats_.fullRebuild = recovered;
    // world 缓存恢复会重新创建全部前端句柄；即使稳定 key 的内容指纹未变，也必须
    // 重新提交直接预览资源。否则首轮 patch 失败并清空 prepare 后，重试可能继续使用
    // 执行端同 key 的旧内容。
    const bool forceDirectPrepare = overlay_state_->forceFullPrepare || recovered;
    bool patched = true;
    for (const PreviewVisualRole role : kPreviewRoles) {
        const size_t index = previewVisualRoleIndex(role);
        OverlayRoleState& state = overlay_state_->roles[index];
        if (geometryDirty[index] && !patchDirectRole(preview_, role, preview_resource_domain_, overlay_world_, state,
                                                     forceDirectPrepare, prepare, last_overlay_sync_stats_)) {
            patched = false;
            break;
        }
        if (referenceDirty[index] &&
            !patchReferenceRole(preview_, scene_, assets_, asset_resource_domain_, preview_resource_domain_, role,
                                overlay_world_, state, last_overlay_sync_stats_)) {
            patched = false;
            break;
        }
    }

    if (!patched) {
        // 正常路径在修改前已验证全部句柄；若操作仍失败，说明缓存与 world 已失配。
        // 清除局部结果后只重试一次全角色恢复，避免发布半完成快照。
        prepare.clear();
        overlay_world_.clear();
        invalidateOverlayHandles(*overlay_state_);
        last_overlay_sync_stats_.reset();
        last_overlay_sync_stats_.fullRebuild = true;
        patched = true;
        for (const PreviewVisualRole role : kPreviewRoles) {
            OverlayRoleState& state = overlay_state_->roles[previewVisualRoleIndex(role)];
            if (!patchDirectRole(preview_, role, preview_resource_domain_, overlay_world_, state, true, prepare,
                                 last_overlay_sync_stats_) ||
                !patchReferenceRole(preview_, scene_, assets_, asset_resource_domain_, preview_resource_domain_, role,
                                    overlay_world_, state, last_overlay_sync_stats_)) {
                patched = false;
                break;
            }
        }
    }
    if (!patched) {
        // 二次失败只可能来自内部状态破坏；不发布悬空句柄，保留 dirty 供下一帧恢复。
        prepare.clear();
        overlay_world_.clear();
        *overlay_state_ = OverlayIncrementalState{};
        overlay_world_snapshot_.reset();
        preview_source_dirty_ = true;
        overlay_reference_source_dirty_ = true;
        last_overlay_sync_stats_.reset();
        last_overlay_sync_stats_.fullRebuild = true;
        return;
    }

    for (const PreviewVisualRole role : kPreviewRoles) {
        OverlayRoleState& state = overlay_state_->roles[previewVisualRoleIndex(role)];
        state.geometryGeneration = preview_ ? preview_->geometryGeneration(role) : 0;
        state.referenceGeneration = preview_ ? preview_->referenceGeneration(role) : 0;
        accumulate(last_overlay_sync_stats_.previewItems, state.directDiagnostics);
        accumulate(last_overlay_sync_stats_.previewItems, state.referenceDiagnostics);
        if (state.directObject) {
            ++last_overlay_sync_stats_.previewObjectCount;
        }
        last_overlay_sync_stats_.previewObjectCount += state.referenceObjects.size();
    }
    overlay_state_->assetChangeCursor = assets_ ? assetChanges.cursorAfterApply() : asset::AssetChangeCursor{};
    overlay_state_->forceFullPrepare = false;
    if (overlay_world_.objectCount() == 0) {
        overlay_world_snapshot_.reset();
    } else {
        overlay_world_snapshot_ = std::make_shared<engine::RenderWorldSnapshot>(overlay_world_.snapshot());
    }
    last_overlay_sync_stats_.worldObjectCount = overlay_world_.objectCount();
    last_overlay_sync_stats_.worldGeometryCount = overlay_world_.geometryCount();
    last_overlay_sync_stats_.worldMaterialCount = overlay_world_.materialCount();
    submission.prepare.merge(prepare);

    last_preview_generation_ = previewGeneration;
    last_overlay_scene_generation_ = sceneGeneration;
    last_overlay_scene_change_domain_ = sceneChangeDomain(scene_);
    preview_source_dirty_ = false;
    overlay_reference_source_dirty_ = false;
}

void RenderSubmissionBuilder::advanceResourceBatch() {
    ++resource_batch_id_;
    if (resource_batch_id_ == 0) {
        resource_batch_id_ = 1;
    }
}

}  // namespace mulan::view
