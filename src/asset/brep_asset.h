/**
 * @file brep_asset.h
 * @brief BRepAsset —— 面向未来编辑能力的 B-Rep 几何资产占位
 * @author hxxcxx
 * @date 2026-07-02
 */

#pragma once

#include "geometry_asset.h"

#include <utility>

namespace mulan::asset {

class BRepAsset : public GeometryAsset {
public:
    explicit BRepAsset(AssetId id, std::string name)
        : GeometryAsset(id, AssetKind::BRep, std::move(name)) {}

    // 预留给未来文档模型使用；当前阶段刻意避免在 asset 头文件中暴露 OCCT。
};

} // namespace mulan::asset
