#include "render_workload.h"

#include "render_contract.h"

namespace mulan::engine {
namespace {

bool targetRoleMatches(const SelectionVisualTarget& target, SelectionVisualRole role) {
    return target.role == role;
}

bool targetMatchesDrawable(const SelectionVisualTarget& target, RenderBucket bucket, PickId pickId,
                           size_t sourceDrawableIndex) {
    if (!target.valid() || target.pickId != pickId) {
        return false;
    }

    if (target.wholeEntity()) {
        return true;
    }

    switch (target.domain) {
    case SelectionVisualDomain::CurveElement:
    case SelectionVisualDomain::CurveSegment:
    case SelectionVisualDomain::CurveVertex:
        if (renderBucketPass(bucket) != RenderPassKind::Edge) {
            return false;
        }
        if (target.hasSourceDrawableIndex) {
            return sourceDrawableIndex == target.sourceDrawableIndex;
        }
        return target.hasPrimitiveIndex && sourceDrawableIndex == target.primitiveIndex;
    case SelectionVisualDomain::MeshFace:
    case SelectionVisualDomain::MeshEdge:
    case SelectionVisualDomain::MeshVertex:
    case SelectionVisualDomain::SurfaceFace:
    case SelectionVisualDomain::SurfaceEdge:
    case SelectionVisualDomain::SurfaceVertex:
    case SelectionVisualDomain::SolidFace:
    case SelectionVisualDomain::SolidEdge:
    case SelectionVisualDomain::SolidVertex: return true;
    case SelectionVisualDomain::ControlPoint:
    case SelectionVisualDomain::Grip:
    case SelectionVisualDomain::Entity: return false;
    }
    return false;
}

}  // namespace

RenderVisualMatch renderVisualMatch(RenderBucket bucket, PickId pickId, size_t sourceDrawableIndex,
                                    const RenderOptions& options) {
    RenderVisualMatch match;
    for (const SelectionVisualTarget& target : options.selectionVisuals.targets()) {
        if (!targetMatchesDrawable(target, bucket, pickId, sourceDrawableIndex)) {
            continue;
        }
        match.selected = match.selected || targetRoleMatches(target, SelectionVisualRole::Selected);
        match.hovered = match.hovered || targetRoleMatches(target, SelectionVisualRole::Hovered);
    }
    return match;
}

}  // namespace mulan::engine
