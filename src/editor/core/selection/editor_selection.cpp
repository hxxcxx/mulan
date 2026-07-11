#include "core/selection/editor_selection.h"

#include <mulan/asset/asset_library.h>
#include <mulan/asset/face_asset.h>
#include <mulan/io/document.h>
#include <mulan/scene/components/geometry_component.h>
#include <mulan/scene/scene.h>

#include <algorithm>
#include <cmath>

namespace mulan::app {
namespace {

const asset::CurveAsset* curveAssetForEntity(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets) {
        return nullptr;
    }

    const scene::GeometryComponent* geometry = scene->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return nullptr;
    }

    return dynamic_cast<const asset::CurveAsset*>(assets->asset(geometry->geometry));
}

const asset::FaceAsset* faceAssetForEntity(const io::Document& document, scene::EntityId entity) {
    const scene::Scene* scene = document.scene();
    const asset::AssetLibrary* assets = document.assets();
    if (!scene || !assets) {
        return nullptr;
    }

    const scene::GeometryComponent* geometry = scene->geometry(entity);
    if (!geometry || !geometry->geometry) {
        return nullptr;
    }

    return dynamic_cast<const asset::FaceAsset*>(assets->asset(geometry->geometry));
}

std::optional<asset::CurveElement> curveElementAt(const asset::CurveAsset& curve, size_t index) {
    const auto& elements = curve.elements();
    if (index >= elements.size()) {
        return std::nullopt;
    }
    return elements[index];
}

size_t parameterSegmentIndex(double parameter) {
    if (!std::isfinite(parameter) || parameter <= 0.0) {
        return 0;
    }
    return static_cast<size_t>(std::floor(parameter));
}

EditorSubEntityKind meshSubEntityKind(EditorPickHitKind kind) {
    switch (kind) {
    case EditorPickHitKind::Face: return EditorSubEntityKind::MeshFace;
    case EditorPickHitKind::Edge: return EditorSubEntityKind::MeshEdge;
    case EditorPickHitKind::Vertex: return EditorSubEntityKind::MeshVertex;
    case EditorPickHitKind::Object:
    case EditorPickHitKind::Curve:
    case EditorPickHitKind::None: return EditorSubEntityKind::Entity;
    }
    return EditorSubEntityKind::Entity;
}

EditorSubEntityKind curveSubEntityKind(const EditorPickHit& pick, asset::CurveElementKind curveKind) {
    if (pick.kind == EditorPickHitKind::Vertex) {
        return EditorSubEntityKind::CurveVertex;
    }
    if (pick.kind == EditorPickHitKind::Edge) {
        return curveKind == asset::CurveElementKind::Polyline ? EditorSubEntityKind::CurveSegment
                                                              : EditorSubEntityKind::CurveElement;
    }
    if (pick.kind == EditorPickHitKind::Curve) {
        return EditorSubEntityKind::CurveElement;
    }
    return EditorSubEntityKind::Entity;
}

bool domainAllowed(const EditorSelectionFilter& filter, EditorSelectionDomain domain) {
    switch (domain) {
    case EditorSelectionDomain::Entity: return filter.allowEntities;
    case EditorSelectionDomain::Curve: return filter.allowCurves;
    case EditorSelectionDomain::Mesh: return filter.allowMeshes;
    case EditorSelectionDomain::Surface: return filter.allowSurfaces;
    case EditorSelectionDomain::Solid: return filter.allowSolids;
    }
    return false;
}

bool kindAllowed(const EditorSelectionFilter& filter, EditorSubEntityKind kind) {
    switch (kind) {
    case EditorSubEntityKind::Entity: return filter.allowEntities;
    case EditorSubEntityKind::CurveElement: return filter.allowCurves;
    case EditorSubEntityKind::CurveSegment:
    case EditorSubEntityKind::MeshEdge:
    case EditorSubEntityKind::SurfaceEdge:
    case EditorSubEntityKind::SolidEdge: return filter.allowEdges;
    case EditorSubEntityKind::CurveVertex:
    case EditorSubEntityKind::MeshVertex:
    case EditorSubEntityKind::SurfaceVertex:
    case EditorSubEntityKind::SolidVertex:
    case EditorSubEntityKind::ControlPoint:
    case EditorSubEntityKind::Grip: return filter.allowVertices;
    case EditorSubEntityKind::MeshFace:
    case EditorSubEntityKind::SurfaceFace:
    case EditorSubEntityKind::SolidFace: return filter.allowFaces;
    }
    return false;
}

}  // namespace

bool sameSelectionReference(const EditorSelectionReference& lhs, const EditorSelectionReference& rhs) {
    return lhs == rhs;
}

void EditorSelectionContext::setFilter(EditorSelectionFilter filter) {
    filter_ = filter;
    if (hovered_ && !accepts(hovered_->reference)) {
        hovered_.reset();
    }

    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [this](const EditorSelectionReference& selected) { return !accepts(selected); }),
                    selected_.end());
}

bool EditorSelectionContext::setHovered(std::optional<EditorSelectionHit> hit) {
    if (hit && (!hit->valid() || !accepts(hit->reference))) {
        hit.reset();
    }

    if (hovered_ && hit && sameSelectionReference(hovered_->reference, hit->reference)) {
        hovered_->pick = hit->pick;
        return false;
    }
    if (!hovered_ && !hit) {
        return false;
    }

    hovered_ = std::move(hit);
    return true;
}

bool EditorSelectionContext::clearHover() {
    if (!hovered_) {
        return false;
    }
    hovered_.reset();
    return true;
}

std::optional<EditorSelectionReference> EditorSelectionContext::primary() const {
    if (selected_.empty()) {
        return std::nullopt;
    }
    return selected_.front();
}

bool EditorSelectionContext::contains(scene::EntityId entity) const {
    return std::any_of(selected_.begin(), selected_.end(),
                       [entity](const EditorSelectionReference& selected) { return selected.entity == entity; });
}

bool EditorSelectionContext::selectSingle(EditorSelectionHit hit) {
    if (!hit.valid()) {
        return clearSelection();
    }
    return selectSingle(hit.reference);
}

bool EditorSelectionContext::selectSingle(EditorSelectionReference reference) {
    if (!reference.valid() || !accepts(reference)) {
        return clearSelection();
    }

    if (selected_.size() == 1 && sameSelectionReference(selected_.front(), reference)) {
        return false;
    }

    selected_.clear();
    selected_.push_back(std::move(reference));
    return true;
}

bool EditorSelectionContext::clearSelection() {
    if (selected_.empty()) {
        return false;
    }
    selected_.clear();
    return true;
}

void EditorSelectionContext::clear() {
    hovered_.reset();
    selected_.clear();
}

bool EditorSelectionContext::accepts(const EditorSelectionReference& reference) const {
    return reference.valid() && domainAllowed(filter_, reference.domain) && kindAllowed(filter_, reference.kind);
}

EditorSelectionHit makeEditorSelectionHit(const EditorPickHit& pick, const io::Document& document) {
    EditorSelectionReference reference{
        .entity = pick.entity,
        .pickId = pick.pickId,
        .domain = EditorSelectionDomain::Entity,
        .kind = EditorSubEntityKind::Entity,
        .subObject =
                SubObjectKey{
                        .sourceDrawableIndex = pick.sourceDrawableIndex,
                        .hasSourceDrawableIndex = true,
                        .primitiveIndex = pick.primitiveIndex,
                        .hasPrimitiveIndex = pick.hasPrimitiveIndex,
                        .parameter = pick.parameter,
                },
    };

    if (!pick.valid()) {
        return EditorSelectionHit{ .reference = reference, .pick = pick };
    }

    if (const asset::CurveAsset* curve = curveAssetForEntity(document, pick.entity); curve && pick.hasPrimitiveIndex) {
        if (const auto element = curveElementAt(*curve, pick.primitiveIndex)) {
            reference.domain = EditorSelectionDomain::Curve;
            reference.subObject.curveElement = element->id;
            reference.subObject.curveKind = element->primitive.kind();
            reference.kind = curveSubEntityKind(pick, reference.subObject.curveKind);
            if (reference.kind == EditorSubEntityKind::CurveSegment) {
                reference.subObject.componentIndex = parameterSegmentIndex(pick.parameter);
                reference.subObject.hasComponentIndex = true;
            } else if (reference.kind == EditorSubEntityKind::CurveVertex) {
                reference.subObject.componentIndex = pick.parameter <= 0.5 ? 0 : 1;
                reference.subObject.hasComponentIndex = true;
            }
            return EditorSelectionHit{ .reference = reference, .pick = pick };
        }
    }

    if (faceAssetForEntity(document, pick.entity)) {
        reference.domain = EditorSelectionDomain::Surface;
        reference.kind = meshSubEntityKind(pick.kind);
        if (reference.kind == EditorSubEntityKind::Entity) {
            reference.domain = EditorSelectionDomain::Entity;
        }
        return EditorSelectionHit{ .reference = reference, .pick = pick };
    }

    reference.domain = EditorSelectionDomain::Mesh;
    reference.kind = meshSubEntityKind(pick.kind);
    if (reference.kind == EditorSubEntityKind::Entity) {
        reference.domain = EditorSelectionDomain::Entity;
    }

    return EditorSelectionHit{ .reference = reference, .pick = pick };
}

}  // namespace mulan::app
