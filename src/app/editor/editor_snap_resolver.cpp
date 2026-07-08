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

}  // namespace

std::optional<EditorPoint> EditorSnapResolver::resolve(const EditorSnapResolveInput& input) {
    const bool canUseGeometry =
            input.pointPolicy.allowGeometry && input.snapSettings.enabled && input.snapSettings.enableGeometrySnap;

    if (canUseGeometry && input.pointPolicy.preferGeometry) {
        for (const auto& candidate : input.candidates) {
            if (candidate.dependsOnGeometry()) {
                return pointFromCandidate(candidate, EditorPointSource::Snap);
            }
        }
    }

    if (input.pointPolicy.allowWorkPlane && input.workPoint) {
        return EditorPoint{
            .world = *input.workPoint,
            .source = EditorPointSource::WorkPlane,
            .dependency = EditorPointDependencyKind::WorkPlane,
            .snapKind = EditorSnapKind::WorkPlane,
        };
    }

    if (canUseGeometry) {
        for (const auto& candidate : input.candidates) {
            if (candidate.dependsOnGeometry()) {
                return pointFromCandidate(candidate, EditorPointSource::Pick);
            }
        }
    }

    return std::nullopt;
}

}  // namespace mulan::app
