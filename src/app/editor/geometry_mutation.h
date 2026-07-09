/**
 * @file geometry_mutation.h
 * @brief 定义几何资产编辑提交的 mutation 数据。
 * @author hxxcxx
 * @date 2026-07-09
 */
#pragma once

#include <mulan/asset/asset_id.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/asset/face_asset.h>
#include <mulan/scene/entity_id.h>

#include <optional>
#include <variant>

namespace mulan::app {

struct CurveElementGeometryMutation {
    asset::CurveElementId element = asset::CurveElementId::invalid();
    asset::CurvePrimitive primitive;
};

struct FaceDefinitionGeometryMutation {
    asset::FaceDefinition face;
};

using GeometryMutation = std::variant<CurveElementGeometryMutation, FaceDefinitionGeometryMutation>;

struct GeometryEditRequest {
    scene::EntityId entity = scene::EntityId::invalid();
    asset::AssetId sourceGeometry = asset::AssetId::invalid();
    asset::AssetId targetGeometry = asset::AssetId::invalid();
    GeometryMutation mutation;
    bool makeUnique = true;
    bool removeSourceGeometryAfterApply = false;
};

struct GeometryEditResult {
    bool changed = false;
    asset::AssetId previousGeometry = asset::AssetId::invalid();
    asset::AssetId appliedGeometry = asset::AssetId::invalid();
    std::optional<GeometryMutation> previousMutation;
    bool createdUniqueGeometry = false;
};

}  // namespace mulan::app
