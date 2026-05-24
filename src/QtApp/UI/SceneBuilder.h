/**
 * @file SceneBuilder.h
 * @brief 从 Document 构建渲染场景（一次性）
 * @author hxxcxx
 * @date 2026-04-23
 *
 * 职责：
 *  - 遍历 Document 的 Entity 树
 *  - 构建 SceneNode 层级（含世界变换和包围盒）
 *  - 为有几何数据的 Entity 创建 SceneNode 并缓存 RenderGeometry
 *  - pickId = entity.id().value，用于拾取时反查
 *
 * 位于 QtApp 层，是唯一同时操作 Document 和 Scene 的构建器。
 */
#pragma once

#include <MulanGeo/Document/Document.h>
#include <MulanGeo/Engine/Scene/Scene.h>

#include <memory>
#include <unordered_map>

class SceneBuilder {
public:
    /// 从 Document 一次性构建 Scene
    static std::unique_ptr<MulanGeo::engine::Scene> build(
        const MulanGeo::document::Document* doc);

    static std::unordered_map<uint32_t, MulanGeo::document::EntityId> buildPickIdMap(
        const MulanGeo::document::Document* doc);
};
