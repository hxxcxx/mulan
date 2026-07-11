#include "editor_snap_resolver.h"

namespace mulan::app {
namespace {

std::optional<EditorPoint> pointFromCandidate(const EditorSnapCandidate& candidate, EditorPointSource source) {
    return EditorPoint{
        .world = candidate.world,
        .source = source,
        .dependency = candidate.dependency,
        .snapKind = candidate.kind,
        .geometry = candidate.geometry,
    };
}

bool candidateAllowed(const EditorSnapCandidate& candidate, const EditorSnapResolveInput& input) {
    switch (candidate.dependency) {
    case EditorPointDependencyKind::Geometry:
        return input.pointPolicy.allowGeometry && input.snapSettings.enabled && input.snapSettings.enableGeometrySnap;
    case EditorPointDependencyKind::Grid:
        return input.pointPolicy.allowWorkPlane && input.snapSettings.enabled && input.snapSettings.enableGridSnap;
    case EditorPointDependencyKind::Axis:
        return input.pointPolicy.allowAxisConstraint && input.snapSettings.enabled &&
               input.snapSettings.enableAxisConstraint;
    case EditorPointDependencyKind::WorkPlane: return input.pointPolicy.allowWorkPlane;
    case EditorPointDependencyKind::Depth: return input.snapSettings.enabled;
    case EditorPointDependencyKind::None: return true;
    }
    return false;
}

bool betterCandidate(const EditorSnapCandidate& candidate, const EditorSnapCandidate& best) {
    constexpr double kPriorityEps = 1.0e-9;
    if (candidate.priority > best.priority + kPriorityEps) {
        return true;
    }
    if (best.priority > candidate.priority + kPriorityEps) {
        return false;
    }
    return candidate.distance < best.distance;
}

}  // namespace

EditorSnapResult EditorSnapResolver::resolveResult(const EditorSnapResolveInput& input) {
    const EditorSnapCandidate* best = nullptr;
    for (const auto& candidate : input.candidates) {
        if (!candidateAllowed(candidate, input)) {
            continue;
        }
        if (!best || betterCandidate(candidate, *best)) {
            best = &candidate;
        }
    }

    if (!best) {
        return {};
    }

    EditorSnapResult result;
    result.candidate = *best;
    if (best->kind == EditorSnapKind::WorkPlane) {
        result.point = pointFromCandidate(*best, EditorPointSource::WorkPlane);
    } else {
        result.point = pointFromCandidate(*best, EditorPointSource::Snap);
    }
    result.resolved = result.point.has_value() && result.point->source == EditorPointSource::Snap;
    return result;
}

std::optional<EditorPoint> EditorSnapResolver::resolve(const EditorSnapResolveInput& input) {
    return resolveResult(input).point;
}

}  // namespace mulan::app
