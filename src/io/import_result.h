/**
 * @file import_result.h
 * @brief 文件导入选项、诊断报告和返回结果。
 * @author hxxcxx
 * @date 2026-07-03
 */
#pragma once

#include <mulan/scene/entity_id.h>

#include <cstddef>
#include <string>
#include <vector>

namespace mulan::io {

struct ImportOptions {
    bool flattenNodeHierarchy = false;
    bool generateMissingNormals = true;
    bool generateMissingTangents = false;
    bool importMaterials = true;
    bool importTextures = true;
    double unitScale = 1.0;
};

struct ImportReport {
    std::vector<std::string> warnings;
    size_t entityCount = 0;
    size_t meshAssetCount = 0;
    size_t brepAssetCount = 0;
    size_t primitiveCount = 0;
    size_t materialCount = 0;
    size_t textureCount = 0;
};

struct ImportResult {
    ImportReport report;
    std::vector<scene::EntityId> entities;
};

} // namespace mulan::io
