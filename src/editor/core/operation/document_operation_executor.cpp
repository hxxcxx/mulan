#include "document_operation_executor.h"

#include "command_history.h"
#include "geometry_edit_service.h"
#include "document/document_session.h"
#include "document/document_editor.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/asset/brep_asset.h>
#include <mulan/asset/face_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/tessellated_asset.h>
#include <mulan/document/document.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/core/profiling/profile.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/components/hierarchy_component.h>
#include <mulan/scene/components/light_component.h>
#include <mulan/scene/components/name_component.h>
#include <mulan/scene/components/render_component.h>
#include <mulan/scene/components/selection_component.h>
#include <mulan/scene/scene.h>

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace mulan::editor {

namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

std::optional<math::Mat4> entityWorldTransform(const Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    if (!scene || !scene->isValid(entity)) {
        return std::nullopt;
    }

    const scene::TransformComponent* transform = scene->transform(entity);
    return transform ? std::optional<math::Mat4>(transform->world) : std::nullopt;
}

bool containsDistinctValidEntities(const Document& document, const std::vector<scene::EntityId>& entities,
                                   bool requireGeometry) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets || entities.empty()) {
        return false;
    }

    std::unordered_set<scene::EntityId> visited;
    visited.reserve(entities.size());
    for (scene::EntityId entity : entities) {
        if (!scene->isValid(entity) || !visited.insert(entity).second) {
            return false;
        }
        if (requireGeometry) {
            const auto* component = scene->geometry(entity);
            if (!component || !component->geometry || !assets->contains(component->geometry)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<GeometryAssetSnapshot> captureGeometryAsset(const asset::AssetLibrary& library, asset::AssetId id) {
    const asset::Asset* source = library.asset(id);
    if (!source)
        return std::nullopt;

    GeometryAssetSnapshot snapshot{ .originalId = id, .name = source->name() };
    if (const auto* curve = dynamic_cast<const asset::CurveAsset*>(source)) {
        snapshot.data = CurveAssetSnapshot{ curve->elements() };
    } else if (const auto* face = dynamic_cast<const asset::FaceAsset*>(source)) {
        snapshot.data = FaceAssetSnapshot{ face->face() };
    } else if (const auto* mesh = dynamic_cast<const asset::MeshAsset*>(source)) {
        snapshot.data = MeshAssetSnapshot{ mesh->primitives() };
    } else if (const auto* tessellated = dynamic_cast<const asset::TessellatedAsset*>(source)) {
        snapshot.data = TessellatedAssetSnapshot{ tessellated->solidMesh(), tessellated->wireMesh() };
    } else if (const auto* brep = dynamic_cast<const asset::BRepAsset*>(source)) {
        snapshot.data = BRepAssetSnapshot{ brep->shape() };
    } else {
        return std::nullopt;
    }
    return snapshot;
}

std::optional<RestoreEntitiesOperation> captureEntities(const Document& document,
                                                        const std::vector<scene::EntityId>& entities,
                                                        bool removeGeometryAssetsOnInverse,
                                                        bool restoreExistingEntities = false,
                                                        bool overwriteExistingAssets = false) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets)
        return std::nullopt;

    RestoreEntitiesOperation restore;
    restore.removeGeometryAssetsOnInverse = removeGeometryAssetsOnInverse;
    restore.restoreExistingEntities = restoreExistingEntities;
    restore.overwriteExistingAssets = overwriteExistingAssets;
    std::unordered_set<asset::AssetId> capturedAssets;

    for (scene::EntityId entity : entities) {
        if (!scene->isValid(entity))
            return std::nullopt;
        const auto* name = scene->name(entity);
        const auto* transform = scene->transform(entity);
        const auto* hierarchy = scene->hierarchy(entity);
        const auto* geometry = scene->geometry(entity);
        const auto* render = scene->render(entity);
        const auto* selection = scene->selection(entity);
        if (!name || !transform || !hierarchy || !geometry || !render || !selection)
            return std::nullopt;

        restore.entities.push_back(EntityStateSnapshot{
                .originalId = entity,
                .name = name->value,
                .localTransform = transform->local,
                .parent = hierarchy->parent,
                .children = scene->childrenOf(entity),
                .geometry = geometry->geometry,
                .visible = render->visible,
                .selected = selection->selected,
                .materialSlots = render->material_slots,
                .light = scene->light(entity) ? std::optional<scene::LightComponent>(*scene->light(entity))
                                              : std::nullopt,
        });
        if (geometry->geometry && capturedAssets.insert(geometry->geometry).second) {
            auto assetSnapshot = captureGeometryAsset(*assets, geometry->geometry);
            if (!assetSnapshot)
                return std::nullopt;
            restore.geometryAssets.push_back(std::move(*assetSnapshot));
        }
    }
    return restore;
}

asset::AssetId mappedAsset(asset::AssetId original, std::span<const AssetIdRemap> remaps) {
    for (const auto& remap : remaps) {
        if (remap.from == original)
            return remap.to;
    }
    return original;
}

scene::EntityId mappedEntity(scene::EntityId original, std::span<const EntityIdRemap> remaps) {
    for (const auto& remap : remaps) {
        if (remap.from == original)
            return remap.to;
    }
    return original;
}

asset::AssetId restoreGeometryAsset(asset::AssetLibrary& library, const GeometryAssetSnapshot& snapshot,
                                    bool overwriteExisting) {
    if (asset::Asset* existing = library.asset(snapshot.originalId)) {
        if (!overwriteExisting)
            return existing->id();
        const bool restored = std::visit(
                [&](const auto& data) {
                    using T = std::decay_t<decltype(data)>;
                    if constexpr (std::is_same_v<T, CurveAssetSnapshot>) {
                        if (auto* asset = dynamic_cast<asset::CurveAsset*>(existing)) {
                            asset->setElements(data.elements);
                            return true;
                        }
                    } else if constexpr (std::is_same_v<T, FaceAssetSnapshot>) {
                        if (auto* asset = dynamic_cast<asset::FaceAsset*>(existing)) {
                            asset->setFace(data.face);
                            return true;
                        }
                    } else if constexpr (std::is_same_v<T, TessellatedAssetSnapshot>) {
                        if (auto* asset = dynamic_cast<asset::TessellatedAsset*>(existing)) {
                            asset->setRenderMeshes(data.solid, data.wire);
                            return true;
                        }
                    } else if constexpr (std::is_same_v<T, BRepAssetSnapshot>) {
                        if (auto* asset = dynamic_cast<asset::BRepAsset*>(existing)) {
                            asset->setShape(data.shape);
                            return true;
                        }
                    } else if constexpr (std::is_same_v<T, MeshAssetSnapshot>) {
                        // Mesh assets are immutable after import; an existing instance is already the captured value.
                        return dynamic_cast<asset::MeshAsset*>(existing) != nullptr;
                    }
                    return false;
                },
                snapshot.data);
        return restored ? existing->id() : asset::AssetId::invalid();
    }

    return std::visit(
            [&](const auto& data) -> asset::AssetId {
                using T = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<T, CurveAssetSnapshot>) {
                    auto* restored = library.create<asset::CurveAsset>(snapshot.name);
                    if (restored)
                        restored->setElements(data.elements);
                    return restored ? restored->id() : asset::AssetId::invalid();
                } else if constexpr (std::is_same_v<T, FaceAssetSnapshot>) {
                    auto* restored = library.create<asset::FaceAsset>(snapshot.name, data.face);
                    return restored ? restored->id() : asset::AssetId::invalid();
                } else if constexpr (std::is_same_v<T, MeshAssetSnapshot>) {
                    auto* restored = library.create<asset::MeshAsset>(snapshot.name, data.primitives);
                    return restored ? restored->id() : asset::AssetId::invalid();
                } else if constexpr (std::is_same_v<T, TessellatedAssetSnapshot>) {
                    auto* restored = library.create<asset::TessellatedAsset>(snapshot.name);
                    if (restored)
                        restored->setRenderMeshes(data.solid, data.wire);
                    return restored ? restored->id() : asset::AssetId::invalid();
                } else {
                    auto* restored = library.create<asset::BRepAsset>(snapshot.name, data.shape);
                    return restored ? restored->id() : asset::AssetId::invalid();
                }
            },
            snapshot.data);
}

}  // namespace

DocumentOperationExecutor::DocumentOperationExecutor(DocumentSession& session)
    : session_(session), history_(session.commandHistory()) {
}

bool DocumentOperationExecutor::execute(DocumentOperation operation) {
    MULAN_PROFILE_ZONE();

    DocumentOperation redoOperation = operation;
    ApplyResult result = apply(std::move(operation));
    if (!result.changed) {
        return false;
    }

    if (result.undoOperation) {
        history_.record(std::move(redoOperation), std::move(*result.undoOperation));
    } else {
        // 未提供可靠逆操作的未来操作仍可成功，但历史不得越过该事务边界。
        history_.recordIrreversibleChange();
    }
    return publish(result);
}

bool DocumentOperationExecutor::undo() {
    MULAN_PROFILE_ZONE();

    std::optional<CommandHistory::Entry> entry = history_.takeUndo();
    if (!entry) {
        return false;
    }

    const ApplyResult result = apply(entry->undoOperation);
    if (!result.changed) {
        history_.restoreUndo(std::move(*entry));
        return false;
    }

    history_.remapReferences(result.entityRemaps, result.assetRemaps);
    remapDocumentOperation(entry->redoOperation, result.entityRemaps, result.assetRemaps);
    remapDocumentOperation(entry->undoOperation, result.entityRemaps, result.assetRemaps);
    if (result.undoOperation)
        entry->redoOperation = *result.undoOperation;
    history_.pushRedo(std::move(*entry));
    return publish(result);
}

bool DocumentOperationExecutor::redo() {
    MULAN_PROFILE_ZONE();

    std::optional<CommandHistory::Entry> entry = history_.takeRedo();
    if (!entry) {
        return false;
    }

    ApplyResult result = apply(entry->redoOperation);
    if (!result.changed) {
        history_.restoreRedo(std::move(*entry));
        return false;
    }
    history_.remapReferences(result.entityRemaps, result.assetRemaps);
    remapDocumentOperation(entry->redoOperation, result.entityRemaps, result.assetRemaps);
    remapDocumentOperation(entry->undoOperation, result.entityRemaps, result.assetRemaps);
    if (result.undoOperation) {
        entry->undoOperation = std::move(*result.undoOperation);
        history_.pushUndo(std::move(*entry));
    } else {
        // 已成功重做但无法再生成逆操作时，清空过期历史，保留当前文档结果。
        history_.recordIrreversibleChange();
    }
    return publish(result);
}

void DocumentOperationExecutor::clearHistory() {
    history_.clear();
}

bool DocumentOperationExecutor::canUndo() const {
    return history_.canUndo();
}

bool DocumentOperationExecutor::canRedo() const {
    return history_.canRedo();
}

DocumentOperationExecutor::ApplyResult DocumentOperationExecutor::apply(DocumentOperation operation) const {
    if (!session_.document()) {
        return {};
    }

    Document& document = *session_.document();
    DocumentEditor editor(document);
    ApplyResult result;

    std::visit(Overloaded{
                       [&editor, &result](CreateCurveOperation& create) {
                           const CurveCreateResult created =
                                   editor.createCurve(std::move(create.name), std::move(create.primitive));
                           result.changed = static_cast<bool>(created);
                           if (result.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                               result.undoOperation = DocumentOperation::removeEntities({ created.entity }, true);
                           }
                       },
                       [&editor, &result](CreateFaceOperation& create) {
                           const scene::EntityId entity =
                                   editor.createFace(std::move(create.name), std::move(create.face));
                           result.changed = static_cast<bool>(entity);
                           if (result.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                               result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                           }
                       },
                       [&editor, &result](CreateMeshOperation& create) {
                           const scene::EntityId entity =
                                   editor.createMesh(std::move(create.name), std::move(create.primitives));
                           result.changed = static_cast<bool>(entity);
                           if (result.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                               result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                           }
                       },
                       [&editor, &result](ExtrudeFaceOperation& op) {
                           auto* ops = modeling::ShapeOpsRegistry::instance().ops();
                           if (!ops) {
                               result.changed = false;
                               return;
                           }
                           auto shapeResult = [&] {
                               MULAN_PROFILE_ZONE_N("ShapeOps::extrude");
                               return ops->extrude(op.params);
                           }();
                           if (!shapeResult) {
                               result.changed = false;
                               return;
                           }
                           const scene::EntityId entity =
                                   editor.createBody(std::move(op.name), std::move(*shapeResult));
                           result.changed = static_cast<bool>(entity);
                           if (result.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                               result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                           }
                       },
                       [&document, &editor, &result](BooleanOperation& op) {
                           auto undo = captureEntities(document, { op.target, op.tool }, true, true, true);
                           if (!undo)
                               return;
                           result.changed = editor.booleanSubtract(op.target, op.tool, op.op);
                           if (result.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                               result.undoOperation = DocumentOperation::restoreEntities(std::move(*undo));
                           }
                       },
                       [&document, &result](UpdateCurveOperation& update) {
                           GeometryEditRequest request{
                               .entity = update.entity,
                               .mutation =
                                       CurveElementGeometryMutation{
                                               .element = update.element,
                                               .primitive = std::move(update.primitive),
                                       },
                           };
                           GeometryEditService service(document);
                           const GeometryEditResult edit = service.apply(std::move(request));
                           result.changed = edit.changed;
                           if (edit.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                           }
                           if (edit.changed && edit.previousMutation) {
                               result.undoOperation = DocumentOperation::updateGeometry(GeometryEditRequest{
                                       .entity = update.entity,
                                       .sourceGeometry = edit.appliedGeometry,
                                       .targetGeometry = edit.previousGeometry,
                                       .mutation = std::move(*edit.previousMutation),
                                       .makeUnique = false,
                                       .removeSourceGeometryAfterApply = edit.createdUniqueGeometry,
                               });
                           }
                       },
                       [&document, &result](UpdateGeometryOperation& update) {
                           const scene::EntityId entity = update.request.entity;
                           GeometryEditService service(document);
                           const GeometryEditResult edit = service.apply(std::move(update.request));
                           result.changed = edit.changed;
                           if (edit.changed) {
                               result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                           }
                           if (edit.changed && edit.previousMutation) {
                               result.undoOperation = DocumentOperation::updateGeometry(GeometryEditRequest{
                                       .entity = entity,
                                       .sourceGeometry = edit.appliedGeometry,
                                       .targetGeometry = edit.previousGeometry,
                                       .mutation = std::move(*edit.previousMutation),
                                       .makeUnique = false,
                                       .removeSourceGeometryAfterApply = edit.createdUniqueGeometry,
                               });
                           }
                       },
                       [&document, &editor, &result](UpdateEntityTransformsOperation& update) {
                           std::vector<scene::EntityId> entities;
                           entities.reserve(update.updates.size());
                           for (const EntityTransformUpdate& item : update.updates) {
                               if (!item.valid()) {
                                   return;
                               }
                               entities.push_back(item.entity);
                           }
                           if (!containsDistinctValidEntities(document, entities, false)) {
                               return;
                           }

                           std::vector<EntityTransformUpdate> previous;
                           previous.reserve(update.updates.size());
                           for (const EntityTransformUpdate& item : update.updates) {
                               if (const std::optional<math::Mat4> transform =
                                           entityWorldTransform(document, item.entity)) {
                                   previous.push_back(EntityTransformUpdate{ item.entity, *transform });
                               }
                           }

                           for (const EntityTransformUpdate& item : update.updates) {
                               result.changed =
                                       editor.updateEntityTransform(item.entity, item.worldTransform) || result.changed;
                           }
                           if (result.changed && !previous.empty()) {
                               result.changes = DocumentChangeKind::Scene;
                               result.undoOperation = DocumentOperation::updateEntityTransforms(std::move(previous));
                           }
                       },
                       [&document, &editor, &result](CopyEntityTransformsOperation& copy) {
                           std::vector<scene::EntityId> sources;
                           sources.reserve(copy.updates.size());
                           for (const EntityTransformUpdate& item : copy.updates) {
                               if (!item.valid()) {
                                   return;
                               }
                               sources.push_back(item.entity);
                           }
                           // 同一源可以复制多次，因此这里只逐项验证，不要求 source 去重。
                           const scene::Scene* scene = document.scene();
                           const asset::AssetLibrary* assets = document.assets();
                           if (!scene || !assets || sources.empty()) {
                               return;
                           }
                           for (scene::EntityId source : sources) {
                               const auto* geometry = scene->geometry(source);
                               if (!scene->isValid(source) || !geometry || !geometry->geometry ||
                                   !assets->contains(geometry->geometry)) {
                                   return;
                               }
                           }

                           std::vector<scene::EntityId> created;
                           created.reserve(copy.updates.size());
                           for (const EntityTransformUpdate& item : copy.updates) {
                               const scene::EntityId entity =
                                       editor.copyEntityWithTransform(item.entity, item.worldTransform);
                               if (entity) {
                                   created.push_back(entity);
                               } else {
                                   // 批量复制是一个事务；中途失败时撤销本批已经创建的实例。
                                   for (scene::EntityId rollback : created) {
                                       editor.removeEntity(rollback, false);
                                   }
                                   return;
                               }
                           }
                           if (!created.empty()) {
                               result.changed = true;
                               result.changes = DocumentChangeKind::Scene;
                               result.undoOperation = DocumentOperation::removeEntities(std::move(created), false);
                           }
                       },
                       [&document, &editor, &result](RemoveEntitiesOperation& remove) {
                           if (!containsDistinctValidEntities(document, remove.entities, false)) {
                               return;
                           }

                           auto undo = captureEntities(document, remove.entities, remove.removeGeometryAssets);
                           if (!undo)
                               return;

                           for (scene::EntityId entity : remove.entities) {
                               // 全量预检后，Scene::destroyEntity 对有效实体是确定成功的。
                               if (!editor.removeEntity(entity, remove.removeGeometryAssets)) {
                                   return;
                               }
                           }
                           result.changed = true;
                           result.changes = DocumentChangeKind::Scene;
                           if (remove.removeGeometryAssets) {
                               result.changes |= DocumentChangeKind::Assets;
                           }
                           result.undoOperation = DocumentOperation::restoreEntities(std::move(*undo));
                       },
                       [&document, &result](RestoreEntitiesOperation& restore) {
                           scene::Scene* scene = document.scene();
                           asset::AssetLibrary* assets = document.assets();
                           if (!scene || !assets || restore.entities.empty())
                               return;

                           std::vector<asset::AssetId> createdAssets;
                           for (const auto& snapshot : restore.geometryAssets) {
                               const bool existed = assets->contains(snapshot.originalId);
                               const asset::AssetId restored =
                                       restoreGeometryAsset(*assets, snapshot, restore.overwriteExistingAssets);
                               if (!restored) {
                                   for (asset::AssetId created : createdAssets)
                                       assets->remove(created);
                                   return;
                               }
                               if (!existed)
                                   createdAssets.push_back(restored);
                               if (restored != snapshot.originalId)
                                   result.assetRemaps.push_back({ snapshot.originalId, restored });
                           }

                           std::vector<scene::EntityId> restoredEntities;
                           std::vector<scene::EntityId> createdEntities;
                           restoredEntities.reserve(restore.entities.size());
                           for (const auto& snapshot : restore.entities) {
                               scene::EntityId restored = snapshot.originalId;
                               if (!restore.restoreExistingEntities || !scene->isValid(restored)) {
                                   restored = scene->createEntity(snapshot.name);
                                   if (!restored) {
                                       for (scene::EntityId created : createdEntities)
                                           scene->destroyEntity(created);
                                       for (asset::AssetId created : createdAssets)
                                           assets->remove(created);
                                       return;
                                   }
                                   createdEntities.push_back(restored);
                               }
                               restoredEntities.push_back(restored);
                               if (restored != snapshot.originalId)
                                   result.entityRemaps.push_back({ snapshot.originalId, restored });
                           }

                           std::vector<asset::AssetId> replacedGeometry;
                           for (size_t i = 0; i < restore.entities.size(); ++i) {
                               const auto& snapshot = restore.entities[i];
                               const scene::EntityId entity = restoredEntities[i];
                               const asset::AssetId geometry = mappedAsset(snapshot.geometry, result.assetRemaps);
                               if (geometry && !assets->contains(geometry))
                                   return;
                               if (const auto* current = scene->geometry(entity);
                                   current && current->geometry && current->geometry != geometry) {
                                   replacedGeometry.push_back(current->geometry);
                               }
                               scene->setName(entity, snapshot.name);
                               scene->setLocalTransform(entity, snapshot.localTransform);
                               scene->setGeometry(entity, geometry);
                               scene->setVisible(entity, snapshot.visible);
                               scene->setMaterialSlots(entity, snapshot.materialSlots);
                               scene->setSelected(entity, snapshot.selected);
                               if (snapshot.light)
                                   scene->setLight(entity, *snapshot.light);
                               else
                                   scene->removeLight(entity);
                           }

                           for (size_t i = 0; i < restore.entities.size(); ++i) {
                               const auto& snapshot = restore.entities[i];
                               scene::EntityId parent = mappedEntity(snapshot.parent, result.entityRemaps);
                               if (!scene->isValid(parent))
                                   parent = scene::EntityId::invalid();
                               scene->setParent(restoredEntities[i], parent);
                           }
                           for (size_t i = 0; i < restore.entities.size(); ++i) {
                               for (scene::EntityId oldChild : restore.entities[i].children) {
                                   const scene::EntityId child = mappedEntity(oldChild, result.entityRemaps);
                                   if (scene->isValid(child) && child != restoredEntities[i])
                                       scene->setParent(child, restoredEntities[i]);
                               }
                           }

                           for (asset::AssetId geometry : replacedGeometry)
                               document.removeGeometryAssetIfUnreferenced(geometry);
                           document.markDirty();
                           result.changed = true;
                           result.changes = DocumentChangeKind::Scene | DocumentChangeKind::Assets;
                           if (!restore.restoreExistingEntities) {
                               result.undoOperation = DocumentOperation::removeEntities(
                                       std::move(restoredEntities), restore.removeGeometryAssetsOnInverse);
                           }
                       },
               },
               operation.data());

    return result;
}

bool DocumentOperationExecutor::publish(const ApplyResult& result) const {
    if (!result.changed || result.changes == DocumentChangeKind::None) {
        return false;
    }
    return session_.publishChange(result.changes).valid();
}

}  // namespace mulan::editor
