#include "document_operation_executor.h"

#include "geometry_edit_service.h"
#include "ui/document_session.h"
#include "ui/document_view_binding.h"

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
#include <utility>

namespace mulan::app {

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
    }

    history_.pushUndo(std::move(*entry));
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

    std::visit(
            Overloaded{
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
                        const scene::EntityId entity = editor.createBody(std::move(op.name), std::move(*shapeResult));
                        result.changed = static_cast<bool>(entity);
                        if (result.changed) {
                            result.undoOperation = DocumentOperation::removeEntities({ entity }, true);
                        }
                    },
                    [&editor, &result](BooleanOperation& op) {
                        // 本轮 undo 简化:布尔更新 target 体、删除 tool 体,不捕获旧 Shape(步骤2完善)。
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
                    [&editor, &result](CopyEntityTransformsOperation& copy) {
                        std::vector<scene::EntityId> created;
                        created.reserve(copy.updates.size());
                        for (const EntityTransformUpdate& item : copy.updates) {
                            const scene::EntityId entity =
                                    editor.copyEntityWithTransform(item.entity, item.worldTransform);
                            if (entity) {
                                created.push_back(entity);
                                result.changed = true;
                            }
                        }
                        if (!created.empty()) {
                            result.undoOperation = DocumentOperation::removeEntities(std::move(created), false);
                        }
                    },
                    [&editor, &result](RemoveEntitiesOperation& remove) {
                        for (scene::EntityId entity : remove.entities) {
                            result.changed = editor.removeEntity(entity, remove.removeGeometryAssets) || result.changed;
                        }
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

}  // namespace mulan::app
