#include "file_manager.h"
#include "file_importer.h"
#include "importer_factory.h"
#include "parsed_scene_loader.h"

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

/// STEP/IGES 路径:把 ShapeFileReader 产出的 NamedShape 列表包成 ParsedScene(brep 分区)。
core::Result<ParsedScene> parseShapeFile(const std::string& path, const std::string& ext) {
    auto reader = modeling::ShapeFileReaderRegistry::instance().create(ext);
    if (!reader) {
        return std::unexpected(
                core::Error::make(core::ErrorCode::NotSupported, "No shape reader for extension: ." + ext));
    }

    auto shapes = reader->read(path);
    if (!shapes)
        return std::unexpected(shapes.error());

    ParsedScene scene;
    for (auto& ns : *shapes) {
        ParsedBRep brep;
        brep.name = ns.name;
        brep.shape = std::move(ns.shape);

        ParsedNode node;
        node.name = ns.name;
        node.parent = SIZE_MAX;
        node.brepIndex = scene.breps.size();

        scene.breps.push_back(std::move(brep));
        scene.nodes.push_back(std::move(node));
        scene.rootNodes.push_back(scene.nodes.size() - 1);
    }
    return scene;
}

}  // namespace

core::Result<OpenDocumentResult> FileManager::openFile(const std::string& path, const ImportOptions& options) {
    const std::string ext = lowerExtension(path);

    std::string displayName = std::filesystem::path(path).filename().string();
    auto doc = std::make_unique<mulan::io::Document>(std::move(displayName));
    doc->setFilePath(path);

    // 解析 → ParsedScene(两条路:io 自有 importer 或 shape reader)
    core::Result<ParsedScene> sceneResult =
            std::unexpected(core::Error::make(core::ErrorCode::NotSupported, "No importer for extension: ." + ext));

    if (auto importer = ImporterFactory::instance().create(ext)) {
        sceneResult = importer->parse(path, options);
    } else if (modeling::ShapeFileReaderRegistry::instance().create(ext)) {
        sceneResult = parseShapeFile(path, ext);
    }

    if (!sceneResult)
        return std::unexpected(sceneResult.error());

    // 装载 → Document
    ParsedSceneLoader loader(*doc);
    auto importResult = loader.load(*sceneResult, options);

    return OpenDocumentResult{ std::move(doc), std::move(importResult) };
}

std::vector<std::string> FileManager::supportedExtensions() const {
    std::vector<std::string> exts = ImporterFactory::instance().allSupportedExtensions();
    for (auto& e : modeling::ShapeFileReaderRegistry::instance().allSupportedExtensions())
        exts.push_back(e);
    return exts;
}

}  // namespace mulan::io
