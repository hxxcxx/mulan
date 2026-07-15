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

namespace mulan::io {

class Document;

class IO_API ParsedSceneLoader {
public:
    explicit ParsedSceneLoader(Document& document);

    ImportResult load(const ParsedScene& scene, const ImportOptions& options = {});

private:
    void loadTextures(const ParsedScene& scene);
    void loadMaterials(const ParsedScene& scene);
    void loadMeshes(const ParsedScene& scene);
    void loadBreps(const ParsedScene& scene);
    void loadNodes(const ParsedScene& scene, const ImportOptions& options);
    void loadNode(size_t nodeIndex, const ParsedScene& scene, scene::EntityId parentEntity,
                  const math::Mat4& parentWorld, const math::Mat4& rootUnitScale, const ImportOptions& options,
                  ImportResult& result);
    asset::AssetId textureAssetId(size_t parsedIndex) const;
    asset::AssetId materialAssetId(size_t parsedIndex) const;
    asset::AssetId meshAssetId(size_t parsedIndex) const;
    asset::AssetId brepAssetId(size_t parsedIndex) const;

    Document& document_;
    ImportReport report_;

    std::vector<asset::AssetId> textureIds_;
    std::vector<asset::AssetId> materialIds_;
    std::vector<asset::AssetId> meshIds_;
    std::vector<asset::AssetId> brepIds_;
    std::vector<scene::EntityId> nodeEntities_;
};

}  // namespace mulan::io
