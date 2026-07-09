#include "document_operation_executor.h"

#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>
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

const asset::CurveAsset* curveAssetFor(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets || !scene->isValid(entity)) {
        return nullptr;
    }

    const scene::GeometryComponent* geometry = scene->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return nullptr;
    }

    return dynamic_cast<const asset::CurveAsset*>(assets->asset(geometry->geometry));
}

std::optional<asset::CurvePrimitive> curvePrimitiveFor(const io::Document& document, scene::EntityId entity,
                                                       asset::CurveElementId elementId) {
    if (!elementId.valid()) {
        return std::nullopt;
    }

    const asset::CurveAsset* curve = curveAssetFor(document, entity);
    if (!curve) {
        return std::nullopt;
    }

    const auto& elements = curve->elements();
    const auto it = std::find_if(elements.begin(), elements.end(),
                                 [elementId](const asset::CurveElement& element) { return element.id == elementId; });
    return it != elements.end() ? std::optional<asset::CurvePrimitive>(it->primitive) : std::nullopt;
}

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
        undo_stack_.push_back(HistoryEntry{ std::move(redoOperation), std::move(*result.undoOperation) });
        redo_stack_.clear();
    }
    return refreshAfterChange(true);
}

bool DocumentOperationExecutor::undo() {
    if (undo_stack_.empty()) {
        return false;
    }

    HistoryEntry entry = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    if (!applyWithoutRecording(entry.undoOperation)) {
        undo_stack_.push_back(std::move(entry));
        return false;
    }

    redo_stack_.push_back(std::move(entry));
    return refreshAfterChange(true);
}

bool DocumentOperationExecutor::redo() {
    if (redo_stack_.empty()) {
        return false;
    }

    HistoryEntry entry = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    ApplyResult result = apply(entry.redoOperation);
    if (!result.changed) {
        redo_stack_.push_back(std::move(entry));
        return false;
    }
    if (result.undoOperation) {
        entry.undoOperation = std::move(*result.undoOperation);
    }

    undo_stack_.push_back(std::move(entry));
    return refreshAfterChange(true);
}

void DocumentOperationExecutor::clearHistory() {
    undo_stack_.clear();
    redo_stack_.clear();
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
                    [&document, &editor, &result](UpdateCurveOperation& update) {
                        std::optional<asset::CurvePrimitive> previous =
                                curvePrimitiveFor(document, update.entity, update.element);
                        if (!previous) {
                            return;
                        }

                        result.changed = editor.updateCurve(update.entity, update.element, std::move(update.primitive));
                        if (result.changed) {
                            result.undoOperation =
                                    DocumentOperation::updateCurve(update.entity, update.element, std::move(*previous));
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
