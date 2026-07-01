/**
 * @file occt_importer.h
 * @brief OCCT 文件导入器 — STEP/IGES 解析后向 Document 添加 B-Rep
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 读取 STEP/IGES，解析为 TopoDS_Shape，调用 Document::addSolid 添加数据。
 * B-Rep 由 Document 层持有，三角化惰性发生（SolidGeometryData）。
 */
#pragma once

#include "file_importer.h"

namespace mulan::document {
class Document;
}

namespace mulan::io {

class IO_API OCCTImporter : public IFileImporter {
public:
    bool import(const std::string& path, mulan::document::Document& doc) override;
    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

} // namespace mulan::io
