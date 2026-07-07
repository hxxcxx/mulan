/**
 * @file curve_mesh_builder.h
 * @brief 从结构化曲线图元构建可渲染线框网格。
 * @author hxxcxx
 * @date 2026-07-07
 */
#pragma once

#include "curve_asset.h"

#include <mulan/graphics/mesh.h>

#include <span>

namespace mulan::asset {

graphics::Mesh buildCurveWireMesh(std::span<const CurvePrimitive> primitives);
graphics::Mesh buildCurveWireMesh(std::span<const CurveElement> elements);

}  // namespace mulan::asset
