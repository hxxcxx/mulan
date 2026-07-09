#include "transform_edit_context.h"

#include <mulan/io/document.h>
#include <mulan/scene/components/transform_component.h>
#include <mulan/scene/scene.h>

namespace mulan::app {
namespace {

TransformEditSubjectKind subjectKindForTarget(const SelectionTarget& target) {
    return target.wholeEntity() ? TransformEditSubjectKind::Entity : TransformEditSubjectKind::SubObject;
}

}  // namespace

TransformEditContext TransformEditContext::fromSelection(const io::Document& document,
                                                         std::span<const SelectionTarget> selection) {
    TransformEditContext context;
    for (const SelectionTarget& target : selection) {
        context.addSubject(document, target);
    }
    return context;
}

TransformEditContext TransformEditContext::fromTarget(const io::Document& document, const SelectionTarget& target) {
    TransformEditContext context;
    context.addSubject(document, target);
    return context;
}

std::vector<EntityTransformUpdate> TransformEditContext::entityUpdates(const math::Mat4& worldDelta) const {
    std::vector<EntityTransformUpdate> updates;
    updates.reserve(subjects_.size());
    for (const TransformEditSubject& subject : subjects_) {
        if (!subject.wholeEntity() || !subject.hasInitialWorldTransform) {
            continue;
        }
        updates.push_back(EntityTransformUpdate{
                .entity = subject.target.entity,
                .worldTransform = worldDelta * subject.initialWorldTransform,
        });
    }
    return updates;
}

void TransformEditContext::addSubject(const io::Document& document, const SelectionTarget& target) {
    if (!target.valid()) {
        return;
    }

    TransformEditSubject subject;
    subject.target = target;
    subject.kind = subjectKindForTarget(target);

    const scene::Scene* scene = document.scene();
    if (scene) {
        if (const scene::TransformComponent* transform = scene->transform(target.entity)) {
            subject.initialWorldTransform = transform->world;
            subject.hasInitialWorldTransform = true;
        }
    }

    subjects_.push_back(subject);
}

}  // namespace mulan::app
