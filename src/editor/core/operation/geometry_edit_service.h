/**
 * @file geometry_edit_service.h
 * @brief GeometryEditService 统一提交几何资产 mutation。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include "geometry_mutation.h"

#include <mulan/document/document.h>

namespace mulan::editor {

class GeometryEditService {
public:
    explicit GeometryEditService(Document& document) : document_(document) {}

    GeometryEditResult apply(GeometryEditRequest request) const;

private:
    std::optional<GeometryMutation> currentMutation(asset::AssetId geometry, const GeometryMutation& mutation) const;
    bool applyMutation(scene::EntityId entity, asset::AssetId geometry, GeometryMutation mutation) const;

    Document& document_;
};

}  // namespace mulan::editor
