#include "document_operation_executor.h"

#include "geometry_edit_service.h"
#include "document/document_session.h"
#include "document/document_view_binding.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/components/transform_component.h>
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

std::optional<math::Mat4> entityWorldTransform(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    if (!scene || !scene->isValid(entity)) {
        return std::nullopt;
    }

    const scene::TransformComponent* transform = scene->transform(entity);
    return transform ? std::optional<math::Mat4>(transform->world) : std::nullopt;
}

bool containsDistinctValidEntities(const io::Document& document, const std::vector<scene::EntityId>& entities,
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

}  // namespace

void DocumentOperationExecutor::bind(DocumentSession* session, DocumentViewBinding* binding) {
    session_ = session;
    binding_ = binding;
    clearHistory();
}

void DocumentOperationExecutor::unbind() {
    clearHistory();
    session_ = nullptr;
    binding_ = nullptr;
}

bool DocumentOperationExecutor::execute(DocumentOperation operation) {
    DocumentOperation redoOperation = operation;
    ApplyResult result = apply(std::move(operation));
    if (!result.changed) {
        return false;
    }

    if (result.undoOperation) {
        history_.record(std::move(redoOperation), std::move(*result.undoOperation));
    } else {
        // Boolean/Delete 等当前没有可靠逆操作。它们仍是成功事务，但历史不得越过该边界。
        history_.recordIrreversibleChange();
    }
    return refreshAfterChange(true);
}

bool DocumentOperationExecutor::undo() {
    std::optional<CommandHistory::Entry> entry = history_.takeUndo();
    if (!entry) {
        return false;
    }

    if (!applyWithoutRecording(entry->undoOperation)) {
        history_.restoreUndo(std::move(*entry));
        return false;
    }

    history_.pushRedo(std::move(*entry));
    return refreshAfterChange(true);
}

bool DocumentOperationExecutor::redo() {
    std::optional<CommandHistory::Entry> entry = history_.takeRedo();
    if (!entry) {
        return false;
    }

    ApplyResult result = apply(entry->redoOperation);
    if (!result.changed) {
        history_.restoreRedo(std::move(*entry));
        return false;
    }
    if (result.undoOperation) {
        entry->undoOperation = std::move(*result.undoOperation);
        history_.pushUndo(std::move(*entry));
    } else {
        // 已成功重做但无法再生成逆操作时，清空过期历史，保留当前文档结果。
        history_.recordIrreversibleChange();
    }
    return refreshAfterChange(true);
}

void DocumentOperationExecutor::clearHistory() {
    history_.clear();
}

DocumentOperationExecutor::ApplyResult DocumentOperationExecutor::apply(DocumentOperation operation) const {
    if (!session_ || !session_->document()) {
        return {};
    }

    io::Document& document = *session_->document();
    io::DocumentEditor editor(document);
    ApplyResult result;

    std::visit(Overloaded{
                       [&editor, &result](CreateCurveOperation& create) {
                           const io::CurveCreateResult created =
                                   editor.createCurve(std::move(create.name), std::move(create.primitive));
                           result.changed = static_cast<bool>(created);
                           if (result.changed) {
                               result.undoOperation = DocumentOperation::removeEntities({ created.entity }, true);
                           }
                       },
                       [&editor, &result](CreateFaceOperation& create) {
                           const scene::EntityId entity =
                                   editor.createFace(std::move(create.name), std::move(create.face));
                           result.changed = static_cast<bool>(entity);
                           if (result.changed) {
                               result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                           }
                       },
                       [&editor, &result](CreateMeshOperation& create) {
                           const scene::EntityId entity =
                                   editor.createMesh(std::move(create.name), std::move(create.primitives));
                           result.changed = static_cast<bool>(entity);
                           if (result.changed) {
                               result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                           }
                       },
                       [&editor, &result](ExtrudeFaceOperation& op) {
                           auto* ops = modeling::ShapeOpsRegistry::instance().ops();
                           if (!ops) {
                               result.changed = false;
                               return;
                           }
                           auto shapeResult = ops->extrude(op.params);
                           if (!shapeResult) {
                               result.changed = false;
                               return;
                           }
                           const scene::EntityId entity =
                                   editor.createBody(std::move(op.name), std::move(*shapeResult));
                           result.changed = static_cast<bool>(entity);
                           if (result.changed) {
                               result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                           }
                       },
                       [&editor, &result](BooleanOperation& op) {
                           // Boolean 当前没有可靠逆操作；execute 会把它登记为不可逆历史边界。
                           result.changed = editor.booleanSubtract(op.target, op.tool, op.op);
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
                               result.undoOperation = DocumentOperation::removeEntities(std::move(created), false);
                           }
                       },
                       [&document, &editor, &result](RemoveEntitiesOperation& remove) {
                           if (!containsDistinctValidEntities(document, remove.entities, false)) {
                               return;
                           }

                           for (scene::EntityId entity : remove.entities) {
                               // 全量预检后，Scene::destroyEntity 对有效实体是确定成功的。
                               if (!editor.removeEntity(entity, remove.removeGeometryAssets)) {
                                   return;
                               }
                           }
                           result.changed = true;
                       },
               },
               operation.data());

    return result;
}

bool DocumentOperationExecutor::applyWithoutRecording(DocumentOperation operation) const {
    return apply(std::move(operation)).changed;
}

bool DocumentOperationExecutor::refreshAfterChange(bool changed) const {
    if (changed && binding_) {
        binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::editor
