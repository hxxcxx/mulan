#include "editor_selection_service.h"

#include <mulan/document/document.h>
#include <mulan/render/frontend/selection_visual_state.h>
#include <mulan/view/core/view_context.h>

namespace mulan::editor {
namespace {

engine::SelectionVisualDomain visualDomain(EditorSelectionDomain domain, EditorSubEntityKind kind) {
    switch (domain) {
    case EditorSelectionDomain::Curve:
        switch (kind) {
        case EditorSubEntityKind::CurveSegment: return engine::SelectionVisualDomain::CurveSegment;
        case EditorSubEntityKind::CurveVertex: return engine::SelectionVisualDomain::CurveVertex;
        case EditorSubEntityKind::CurveElement: return engine::SelectionVisualDomain::CurveElement;
        case EditorSubEntityKind::ControlPoint: return engine::SelectionVisualDomain::ControlPoint;
        case EditorSubEntityKind::Grip: return engine::SelectionVisualDomain::Grip;
        case EditorSubEntityKind::Entity:
        case EditorSubEntityKind::MeshFace:
        case EditorSubEntityKind::MeshEdge:
        case EditorSubEntityKind::MeshVertex:
        case EditorSubEntityKind::SurfaceFace:
        case EditorSubEntityKind::SurfaceEdge:
        case EditorSubEntityKind::SurfaceVertex:
        case EditorSubEntityKind::SolidFace:
        case EditorSubEntityKind::SolidEdge:
        case EditorSubEntityKind::SolidVertex: return engine::SelectionVisualDomain::Entity;
        }
        break;
    case EditorSelectionDomain::Mesh:
        switch (kind) {
        case EditorSubEntityKind::MeshFace: return engine::SelectionVisualDomain::MeshFace;
        case EditorSubEntityKind::MeshEdge: return engine::SelectionVisualDomain::MeshEdge;
        case EditorSubEntityKind::MeshVertex: return engine::SelectionVisualDomain::MeshVertex;
        case EditorSubEntityKind::Entity:
        case EditorSubEntityKind::CurveElement:
        case EditorSubEntityKind::CurveSegment:
        case EditorSubEntityKind::CurveVertex:
        case EditorSubEntityKind::SurfaceFace:
        case EditorSubEntityKind::SurfaceEdge:
        case EditorSubEntityKind::SurfaceVertex:
        case EditorSubEntityKind::SolidFace:
        case EditorSubEntityKind::SolidEdge:
        case EditorSubEntityKind::SolidVertex:
        case EditorSubEntityKind::ControlPoint:
        case EditorSubEntityKind::Grip: return engine::SelectionVisualDomain::Entity;
        }
        break;
    case EditorSelectionDomain::Surface:
        if (kind == EditorSubEntityKind::MeshFace || kind == EditorSubEntityKind::SurfaceFace) {
            return engine::SelectionVisualDomain::SurfaceFace;
        }
        if (kind == EditorSubEntityKind::MeshEdge || kind == EditorSubEntityKind::SurfaceEdge) {
            return engine::SelectionVisualDomain::SurfaceEdge;
        }
        if (kind == EditorSubEntityKind::MeshVertex || kind == EditorSubEntityKind::SurfaceVertex) {
            return engine::SelectionVisualDomain::SurfaceVertex;
        }
        break;
    case EditorSelectionDomain::Solid:
        if (kind == EditorSubEntityKind::MeshFace || kind == EditorSubEntityKind::SolidFace) {
            return engine::SelectionVisualDomain::SolidFace;
        }
        if (kind == EditorSubEntityKind::MeshEdge || kind == EditorSubEntityKind::SolidEdge) {
            return engine::SelectionVisualDomain::SolidEdge;
        }
        if (kind == EditorSubEntityKind::MeshVertex || kind == EditorSubEntityKind::SolidVertex) {
            return engine::SelectionVisualDomain::SolidVertex;
        }
        break;
    case EditorSelectionDomain::Entity: break;
    }
    return engine::SelectionVisualDomain::Entity;
}

engine::SelectionVisualTarget visualTarget(const EditorSelectionReference& reference,
                                           engine::SelectionVisualRole role) {
    engine::SelectionVisualTarget target;
    target.pickId = reference.renderPickId();
    target.role = role;
    target.domain = visualDomain(reference.domain, reference.kind);

    if (reference.subObject.hasSourceDrawableIndex) {
        target.sourceDrawableIndex = static_cast<uint32_t>(reference.subObject.sourceDrawableIndex);
        target.hasSourceDrawableIndex = true;
    }
    if (reference.subObject.hasPrimitiveIndex) {
        target.primitiveIndex = static_cast<uint32_t>(reference.subObject.primitiveIndex);
        target.hasPrimitiveIndex = true;
    }
    if (reference.subObject.hasComponentIndex) {
        target.componentIndex = static_cast<uint32_t>(reference.subObject.componentIndex);
        target.hasComponentIndex = true;
    }
    return target;
}

}  // namespace

EditorSelectionService::EditorSelectionService(view::ViewContext& view) : view_(view) {
    syncVisualState();
}

EditorSelectionService::~EditorSelectionService() {
    clear();
    view_.clearSelectionVisualState();
}

void EditorSelectionService::clear() {
    context_.clear();
    syncVisualState();
}

void EditorSelectionService::clearHover() {
    context_.clearHover();
    syncVisualState();
}

void EditorSelectionService::setHovered(std::optional<EditorSelectionHit> hit) {
    context_.setHovered(hit);
    syncVisualState();
}

void EditorSelectionService::selectSingleAndHover(EditorSelectionHit hit) {
    context_.selectSingle(hit);
    context_.setHovered(hit);
    syncVisualState();
}

void EditorSelectionService::clearSelectionAndHover() {
    context_.clearSelection();
    context_.clearHover();
    syncVisualState();
}

void EditorSelectionService::setFilter(EditorSelectionFilter filter) {
    context_.setFilter(filter);
    context_.clearHover();
    syncVisualState();
}

bool EditorSelectionService::pruneInvalid(const Document& document) {
    const bool changed = context_.pruneInvalid(document);
    if (changed) {
        syncVisualState();
    }
    return changed;
}

void EditorSelectionService::syncVisualState() {
    engine::SelectionVisualState state;
    for (const EditorSelectionReference& selected : context_.selected()) {
        state.add(visualTarget(selected, engine::SelectionVisualRole::Selected));
    }
    if (const auto& hovered = context_.hovered(); hovered && hovered->valid()) {
        state.add(visualTarget(hovered->reference, engine::SelectionVisualRole::Hovered));
    }
    view_.setSelectionVisualState(std::move(state));
}

}  // namespace mulan::editor
