/**
 * @file parsed_scene_loader.h
 * @brief ParsedSceneLoader —— 把中立 ParsedScene 灌进 Document。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 统一的"装文档"步骤:先建资源资产(纹理/材质/网格/B-Rep),再递归建节点图
 * (实体 + 父子 + 本地变换 + 资源绑定)。策略在此统一:
 *   - 变换统一用 setLocalTransform(从节点 local 链算 world)
 *   - unitScale 应用到根节点,不烤进解析数据
 *   - 包围盒由 RenderScene 从 GeometryAsset + world transform 唯一派生
 */
#pragma once

#include "import_result.h"
#include "io_export.h"
#include "parsed_scene.h"

#include <mulan/asset/asset_id.h>
#include <mulan/scene/entity_id.h>

#include <vector>

namespace mulan::core {
class ThreadPool;
}

namespace mulan {
class Document;
}

namespace mulan::io {

class IO_API ParsedSceneLoader {
public:
    ParsedSceneLoader(mulan::Document& document, core::ThreadPool& workerPool);

    /// 消费解析结果并把其中的大块资源所有权移交给 Document；调用后 scene 不再可复用。
    ImportResult load(ParsedScene&& scene, const ImportOptions& options = {});

private:
    void loadTextures(ParsedScene& scene);
    void loadMaterials(ParsedScene& scene);
    void loadMeshes(ParsedScene& scene);
    void loadBreps(ParsedScene& scene);
    void loadNodes(const ParsedScene& scene, const ImportOptions& options);
    void loadNode(size_t nodeIndex, const ParsedScene& scene, scene::EntityId parentEntity,
                  const math::Mat4& parentWorld, const math::Mat4& rootUnitScale, const ImportOptions& options,
                  const std::vector<std::vector<size_t>>& children, std::vector<uint8_t>& visitState, size_t depth,
                  ImportResult& result);
    asset::AssetId textureAssetId(size_t parsedIndex) const;
    asset::AssetId materialAssetId(size_t parsedIndex) const;
    asset::AssetId meshAssetId(size_t parsedIndex) const;
    asset::AssetId brepAssetId(size_t parsedIndex) const;

    mulan::Document& document_;
    core::ThreadPool& worker_pool_;
    ImportReport report_;

    std::vector<asset::AssetId> textureIds_;
    std::vector<uint8_t> textureHasTransparency_;
    std::vector<asset::AssetId> materialIds_;
    std::vector<asset::AssetId> meshIds_;
    std::vector<asset::AssetId> brepIds_;
    std::vector<scene::EntityId> nodeEntities_;
};

}  // namespace mulan::io
