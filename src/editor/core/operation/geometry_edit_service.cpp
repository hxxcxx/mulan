#include "core/operation/geometry_edit_service.h"

#include <mulan/asset/asset_library.h>
#include <mulan/io/document_editor.h>

#include <algorithm>
#include <utility>

namespace mulan::app {
namespace {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

const asset::CurveAsset* curveAsset(const io::Document& document, asset::AssetId geometry) {
    const asset::AssetLibrary* assets = document.assets();
    return assets ? dynamic_cast<const asset::CurveAsset*>(assets->asset(geometry)) : nullptr;
}

const asset::FaceAsset* faceAsset(const io::Document& document, asset::AssetId geometry) {
    const asset::AssetLibrary* assets = document.assets();
    return assets ? dynamic_cast<const asset::FaceAsset*>(assets->asset(geometry)) : nullptr;
}

std::optional<asset::CurvePrimitive> curvePrimitive(const asset::CurveAsset& curve, asset::CurveElementId elementId) {
    const auto& elements = curve.elements();
    const auto it = std::find_if(elements.begin(), elements.end(),
                                 [elementId](const asset::CurveElement& element) { return element.id == elementId; });
    if (it == elements.end()) {
        return std::nullopt;
    }
    return it->primitive;
}

}  // namespace

GeometryEditResult GeometryEditService::apply(GeometryEditRequest request) const {
    io::DocumentEditor editor(document_);
    GeometryEditResult result;

    const asset::AssetId currentGeometry = editor.geometryAssetForEntity(request.entity);
    if (!currentGeometry) {
        return result;
    }
    if (request.sourceGeometry && request.sourceGeometry != currentGeometry) {
        return result;
    }

    result.previousGeometry = currentGeometry;
    result.previousMutation = currentMutation(currentGeometry, request.mutation);
    if (!result.previousMutation) {
        return result;
    }

    asset::AssetId editGeometry = currentGeometry;
    if (request.targetGeometry && request.targetGeometry != currentGeometry) {
        if (!editor.setEntityGeometry(request.entity, request.targetGeometry)) {
            return result;
        }
        editGeometry = request.targetGeometry;
    } else if (request.makeUnique && editor.geometryReferenceCount(currentGeometry) > 1) {
        const asset::AssetId uniqueGeometry = editor.duplicateGeometryAsset(currentGeometry, " Edit");
        if (!uniqueGeometry || !editor.setEntityGeometry(request.entity, uniqueGeometry)) {
            if (uniqueGeometry) {
                editor.removeGeometryAsset(uniqueGeometry);
            }
            return result;
        }
        editGeometry = uniqueGeometry;
        result.createdUniqueGeometry = true;
    }

    if (!applyMutation(request.entity, editGeometry, std::move(request.mutation))) {
        if (editGeometry != currentGeometry) {
            editor.setEntityGeometry(request.entity, currentGeometry);
            if (result.createdUniqueGeometry) {
                editor.removeGeometryAsset(editGeometry);
            }
        }
        return result;
    }

    if (request.removeSourceGeometryAfterApply && currentGeometry != editGeometry) {
        editor.removeGeometryAsset(currentGeometry);
    }

    result.changed = true;
    result.appliedGeometry = editGeometry;
    return result;
}

std::optional<GeometryMutation> GeometryEditService::currentMutation(asset::AssetId geometry,
                                                                     const GeometryMutation& mutation) const {
    return std::visit(
            Overloaded{
                    [this,
                     geometry](const CurveElementGeometryMutation& curveMutation) -> std::optional<GeometryMutation> {
                        const asset::CurveAsset* curve = curveAsset(document_, geometry);
                        if (!curve || !curveMutation.element.valid()) {
                            return std::nullopt;
                        }
                        std::optional<asset::CurvePrimitive> primitive = curvePrimitive(*curve, curveMutation.element);
                        if (!primitive) {
                            return std::nullopt;
                        }
                        return GeometryMutation(CurveElementGeometryMutation{
                                .element = curveMutation.element,
                                .primitive = std::move(*primitive),
                        });
                    },
                    [this, geometry](const FaceDefinitionGeometryMutation&) -> std::optional<GeometryMutation> {
                        const asset::FaceAsset* face = faceAsset(document_, geometry);
                        if (!face) {
                            return std::nullopt;
                        }
                        return GeometryMutation(FaceDefinitionGeometryMutation{ .face = face->face() });
                    },
            },
            mutation);
}

bool GeometryEditService::applyMutation(scene::EntityId entity, asset::AssetId geometry,
                                        GeometryMutation mutation) const {
    io::DocumentEditor editor(document_);
    return std::visit(Overloaded{
                              [&editor, entity, geometry](CurveElementGeometryMutation& curve) {
                                  return editor.updateCurveAsset(entity, geometry, curve.element,
                                                                 std::move(curve.primitive));
                              },
                              [&editor, entity, geometry](FaceDefinitionGeometryMutation& face) {
                                  return editor.updateFaceAsset(entity, geometry, std::move(face.face));
                              },
                      },
                      mutation);
}

}  // namespace mulan::app
