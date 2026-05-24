/**
 * @file OCCTImporter.h
 * @brief OCCT 文件导入器 — STEP/IGES 解析为 Entity
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 读取 STEP/IGES，解析为 TopoDS_Shape，
 * 包装为 OCCTShapeGeometry 存入 Document 的 Entity 中。
 * 保留原始 B-Rep 数据，不做三角化。
 */
#pragma once

#include "IFileImporter.h"

namespace mulan::document {

class DOCUMENT_API OCCTImporter : public IFileImporter {
public:
    ImportResult importFile(const std::string& path) override;
    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

} // namespace mulan::Document
