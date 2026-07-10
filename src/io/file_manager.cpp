#include "file_manager.h"
#include "file_importer.h"
#include "importer_factory.h"

#include <mulan/core/result/error.h>
#include <mulan/io/document.h>
#include <mulan/modeling/core/shape_file_reader.h>

#include <algorithm>
#include <cctype>
#include <expected>
#include <utility>

namespace mulan::io {

namespace {

std::string lowerExtension(std::string_view path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext = ext.substr(1);
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

}  // namespace

core::Result<OpenDocumentResult> FileManager::openFile(const std::string& path, const ImportOptions& options) {
    const std::string ext = lowerExtension(path);

    std::string displayName = std::filesystem::path(path).filename().string();
    auto doc = std::make_unique<mulan::io::Document>(std::move(displayName));
    doc->setFilePath(path);

    ImportResult importResult;

    // 1) io 自有导入器：glTF/Assimp 等非 B-Rep 格式。
    if (auto importer = ImporterFactory::instance().create(ext)) {
        auto result = importer->import(path, *doc, options);
        if (!result)
            return std::unexpected(result.error());
        importResult = std::move(*result);
    }
    // 2) modeling_core 形状读取器：STEP/IGES 等 B-Rep 格式（后端经虚分发读取）。
    else if (auto reader = modeling::ShapeFileReaderRegistry::instance().create(ext)) {
        auto shapes = reader->read(path);
        if (!shapes)
            return std::unexpected(shapes.error());

        importResult.entities.reserve((*shapes).size());
        for (auto& ns : *shapes) {
            if (auto id = doc->addBody(std::move(ns.shape), std::move(ns.name)))
                importResult.entities.push_back(id);
        }
        importResult.report.entityCount = importResult.entities.size();
        importResult.report.brepAssetCount = importResult.entities.size();
    } else {
        return std::unexpected(core::Error::make(core::ErrorCode::NotSupported, "No importer for extension: ." + ext));
    }

    return OpenDocumentResult{ std::move(doc), std::move(importResult) };
}

std::vector<std::string> FileManager::supportedExtensions() const {
    std::vector<std::string> exts = ImporterFactory::instance().allSupportedExtensions();
    for (auto& e : modeling::ShapeFileReaderRegistry::instance().allSupportedExtensions())
        exts.push_back(e);
    return exts;
}

}  // namespace mulan::io
