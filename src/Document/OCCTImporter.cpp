/**
 * @file OCCTImporter.cpp
 * @brief OCCT 文件导入器实现 — 保留 B-Rep，不做三角化
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "OCCTImporter.h"
#include "Document.h"
#include "OCCTShapeGeometry.h"
#include "ImporterFactory.h"

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Interface_Static.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopAbs.hxx>

#include <algorithm>
#include <clocale>
#include <filesystem>

namespace MulanGeo::document {

namespace {

TopoDS_Shape readSTEP(const std::string& path) {
    STEPControl_Reader reader;
    Interface_Static::SetIVal("read.stepbspline.continuity", 2);

    IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    if (status != IFSelect_RetDone) {
        throw std::runtime_error("Failed to read STEP file: " + path);
    }

    reader.TransferRoots();
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        throw std::runtime_error("No valid shape found in STEP file");
    }
    return shape;
}

TopoDS_Shape readIGES(const std::string& path) {
    IGESControl_Reader reader;
    IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    if (status != IFSelect_RetDone) {
        throw std::runtime_error("Failed to read IGES file: " + path);
    }

    reader.TransferRoots();
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        throw std::runtime_error("No valid shape found in IGES file");
    }
    return shape;
}

TopoDS_Shape readFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".step" || ext == ".stp") return readSTEP(path);
    if (ext == ".iges" || ext == ".igs") return readIGES(path);

    throw std::runtime_error("Unsupported format: " + ext);
}

void populateDocument(const TopoDS_Shape& shape, Document& doc) {
    int partIndex = 0;

    if (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID) {
        bool hasSolids = false;
        for (TopExp_Explorer exp(shape, TopAbs_SOLID); exp.More(); exp.Next()) {
            hasSolids = true;
            ++partIndex;
            std::string name = "Part_" + std::to_string(partIndex);
            auto geo = std::make_unique<OCCTShapeGeometry>(exp.Current());
            doc.createEntity(std::move(name), std::move(geo));
        }

        if (!hasSolids) {
            for (TopExp_Explorer exp(shape, TopAbs_SHELL); exp.More(); exp.Next()) {
                ++partIndex;
                std::string name = "Shell_" + std::to_string(partIndex);
                auto geo = std::make_unique<OCCTShapeGeometry>(exp.Current());
                doc.createEntity(std::move(name), std::move(geo));
            }

            if (partIndex == 0) {
                ++partIndex;
                auto geo = std::make_unique<OCCTShapeGeometry>(shape);
                doc.createEntity("Shape_1", std::move(geo));
            }
        }
    } else {
        ++partIndex;
        auto geo = std::make_unique<OCCTShapeGeometry>(shape);
        doc.createEntity("Shape_1", std::move(geo));
    }
}

} // anonymous namespace

ImportResult OCCTImporter::importFile(const std::string& path) {
    ImportResult result;

    try {
        auto* oldLocale = std::setlocale(LC_NUMERIC, "C");

        TopoDS_Shape shape = readFile(path);

        auto doc = Document::create();
        std::filesystem::path p(path);
        doc->setFilePath(path);
        doc->setDisplayName(p.filename().string());

        populateDocument(shape, *doc);

        if (oldLocale) std::setlocale(LC_NUMERIC, oldLocale);

        result.document = std::move(doc);
        result.success = true;
    } catch (const std::exception& e) {
        result.error = e.what();
        result.success = false;
    }

    return result;
}

std::vector<std::string> OCCTImporter::supportedExtensions() const {
    return {"step", "stp", "iges", "igs"};
}

std::string OCCTImporter::name() const {
    return "OCCT Importer";
}

// --- 自动注册 ---
static AutoRegisterImporter _reg_step("step", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<OCCTImporter>();
});
static AutoRegisterImporter _reg_stp("stp", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<OCCTImporter>();
});
static AutoRegisterImporter _reg_iges("iges", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<OCCTImporter>();
});
static AutoRegisterImporter _reg_igs("igs", []() -> std::unique_ptr<IFileImporter> {
    return std::make_unique<OCCTImporter>();
});

} // namespace MulanGeo::Document
