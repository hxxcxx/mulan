#include "document_operation_executor.h"

#include "ui/document_session.h"
#include "ui/document_view_binding.h"

#include <mulan/io/document.h>
#include <mulan/io/document_editor.h>

#include <utility>

namespace mulan::app {

namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

}  // namespace

void DocumentOperationExecutor::bind(DocumentSession* session, DocumentViewBinding* binding) {
    session_ = session;
    binding_ = binding;
}

void DocumentOperationExecutor::unbind() {
    session_ = nullptr;
    binding_ = nullptr;
}

bool DocumentOperationExecutor::execute(DocumentOperation operation) const {
    if (!session_ || !session_->document()) {
        return false;
    }

    io::DocumentEditor editor(*session_->document());
    bool changed = false;
    std::visit(Overloaded{
                       [&editor, &changed](CreateCurveOperation& create) {
                           changed = static_cast<bool>(
                                   editor.createCurve(std::move(create.name), std::move(create.primitive)));
                       },
                       [&editor, &changed](CreateFaceOperation& create) {
                           changed =
                                   static_cast<bool>(editor.createFace(std::move(create.name), std::move(create.face)));
                       },
                       [&editor, &changed](CreateMeshOperation& create) {
                           changed = static_cast<bool>(
                                   editor.createMesh(std::move(create.name), std::move(create.primitives)));
                       },
                       [&editor, &changed](UpdateCurveOperation& update) {
                           changed = editor.updateCurve(update.entity, update.element, std::move(update.primitive));
                       },
                       [&editor, &changed](UpdateEntityTransformsOperation& update) {
                           for (const EntityTransformUpdate& item : update.updates) {
                               changed = editor.updateEntityTransform(item.entity, item.worldTransform) || changed;
                           }
                       },
               },
               operation.data());

    if (changed && binding_) {
        binding_->refresh();
    }
    return changed;
}

}  // namespace mulan::app
