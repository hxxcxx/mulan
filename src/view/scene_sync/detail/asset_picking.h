/**
 * @file asset_picking.h
 * @brief 资产级拾取：遍历资产 drawable / 曲线图元，委托 mesh/curve picking。
 * @author hxxcxx
 * @date 2026-07-11
 */
#pragma once

#include "picking_types.h"

#include <mulan/asset/asset_library.h>
#include <mulan/math/math.h>

#include <vector>

namespace mulan::view::detail {

class PrimitivePickIndexCache;
struct PrimitivePickQueryStats;

/// 资产级最近拾取：返回整个资产中最近的命中。
MeshPickResult pickGeometryAsset(const math::Ray3& ray, const asset::Asset& asset, const math::Mat4& worldTransform,
                                 double lineToleranceWorld, PrimitivePickIndexCache* indexCache = nullptr,
                                 PrimitivePickQueryStats* indexStats = nullptr);

/// 资产级候选收集：返回资产中全部命中候选。
void appendGeometryAssetPickCandidates(const math::Ray3& ray, const asset::Asset& asset,
                                       const math::Mat4& worldTransform, double lineToleranceWorld,
                                       std::vector<MeshPickResult>& out, PrimitivePickIndexCache* indexCache = nullptr,
                                       PrimitivePickQueryStats* indexStats = nullptr);

/// 把中间拾取结果转换为 RenderScene::PickResult。
RenderScene::PickResult pickResultFromMeshHit(scene::EntityId id, const SceneProxy& proxy,
                                              const MeshPickResult& meshHit, double lineToleranceWorld);

}  // namespace mulan::view::detail
