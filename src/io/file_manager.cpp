#include "file_manager.h"
#include "file_importer.h"
#include "importer_factory.h"
#include "parsed_scene_loader.h"

#include <mulan/core/result/error.h>
#include <mulan/core/log/log.h>
#include <mulan/io/document.h>
#include <mulan/modeling/core/shape_file_reader.h>

#include <algorithm>
#include <chrono>
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
Result<ParsedScene> parseShapeFile(const std::string& path, const std::string& ext) {
    auto reader = modeling::ShapeFileReaderRegistry::instance().create(ext);
    if (!reader) {
        return std::unexpected(Error::make(ErrorCode::NotSupported, "No shape reader for extension: ." + ext));
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

Result<OpenDocumentResult> FileManager::openFile(const std::string& path, const ImportOptions& options) {
    const auto startedAt = std::chrono::steady_clock::now();
    const std::string ext = lowerExtension(path);
    LOG_INFO("[IO] Import started: path={}, extension={}", path, ext.empty() ? "<none>" : ext);

    std::string displayName = std::filesystem::path(path).filename().string();
    auto doc = std::make_unique<mulan::io::Document>(std::move(displayName));
    doc->setFilePath(path);

    // 解析 → ParsedScene(两条路:io 自有 importer 或 shape reader)
    Result<ParsedScene> sceneResult =
            std::unexpected(Error::make(ErrorCode::NotSupported, "No importer for extension: ." + ext));

    const char* importerKind = "none";
    if (auto importer = ImporterFactory::instance().create(ext)) {
        importerKind = "scene";
        sceneResult = importer->parse(path, options);
    } else if (modeling::ShapeFileReaderRegistry::instance().create(ext)) {
        importerKind = "shape";
        sceneResult = parseShapeFile(path, ext);
    }

    if (!sceneResult) {
        const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt)
                        .count();
        LOG_ERROR("[IO] Import failed: path={}, extension={}, importer={}, elapsedMs={}, error={}", path,
                  ext.empty() ? "<none>" : ext, importerKind, elapsed, sceneResult.error().message);
        return std::unexpected(sceneResult.error());
    }

    LOG_DEBUG("[IO] Parsed scene: importer={}, nodes={}, meshes={}, breps={}, materials={}, textures={}, lights={}",
              importerKind, sceneResult->nodes.size(), sceneResult->meshes.size(), sceneResult->breps.size(),
              sceneResult->materials.size(), sceneResult->textures.size(), sceneResult->lights.size());

    // 装载 → Document
    ParsedSceneLoader loader(*doc);
    auto importResult = loader.load(*sceneResult, options);

    const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt).count();
    LOG_INFO(
            "[IO] Import completed: path={}, importer={}, elapsedMs={}, entities={}, meshes={}, breps={}, "
            "primitives={}, materials={}, textures={}, lights={}, warnings={}",
            path, importerKind, elapsed, importResult.report.entityCount, importResult.report.meshAssetCount,
            importResult.report.brepAssetCount, importResult.report.primitiveCount, importResult.report.materialCount,
            importResult.report.textureCount, importResult.report.lightCount, importResult.report.warnings.size());

    return OpenDocumentResult{ std::move(doc), std::move(importResult) };
}

std::vector<std::string> FileManager::supportedExtensions() const {
    std::vector<std::string> exts = ImporterFactory::instance().allSupportedExtensions();
    for (auto& e : modeling::ShapeFileReaderRegistry::instance().allSupportedExtensions())
        exts.push_back(e);
    return exts;
}

}  // namespace mulan::io
