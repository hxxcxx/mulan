#include "core/operation/drag_edit_session.h"

#include <utility>

namespace mulan::app {

DragEditSession::DragEditSession(DragEditDescriptor descriptor)
    : descriptor_(std::move(descriptor)), start_local_(descriptor_.startWorld.transformedBy(descriptor_.worldToLocal)) {
}

DragEditSample DragEditSession::sampleAt(const math::Point3& currentWorld) const {
    const math::Point3 currentLocal = currentWorld.transformedBy(descriptor_.worldToLocal);
    return DragEditSample{
        .target = descriptor_.target,
        .subjectKind = descriptor_.subjectKind,
        .startWorld = descriptor_.startWorld,
        .currentWorld = currentWorld,
        .deltaWorld = currentWorld - descriptor_.startWorld,
        .startLocal = start_local_,
        .currentLocal = currentLocal,
        .deltaLocal = currentLocal - start_local_,
    };
}

DragEditSample DragEditSession::update(const math::Point3& currentWorld) {
    last_sample_ = sampleAt(currentWorld);
    return *last_sample_;
}

}  // namespace mulan::app
