#include "occt_importer.h"
#include "importer_factory.h"

#include <mulan/core/result/error.h>
#include <mulan/io/document.h>

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Interface_Static.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <cctype>
#include <clocale>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace mulan::io {

namespace {

class NumericLocaleGuard {
public:
    NumericLocaleGuard()
        : old_(std::setlocale(LC_NUMERIC, nullptr) ? std::setlocale(LC_NUMERIC, nullptr) : "")
    {
        std::setlocale(LC_NUMERIC, "C");
    }

    ~NumericLocaleGuard() {
        if (!old_.empty()) {
            std::setlocale(LC_NUMERIC, old_.c_str());
        }
    }

    NumericLocaleGuard(const NumericLocaleGuard&) = delete;
    NumericLocaleGuard& operator=(const NumericLocaleGuard&) = delete;

private:
    std::string old_;
};

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

ImportResult populateDocument(const TopoDS_Shape& shape, mulan::io::Document& doc) {
    ImportResult result;
    int partIndex = 0;

    if (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID) {
        bool hasSolids = false;
        for (TopExp_Explorer exp(shape, TopAbs_SOLID); exp.More(); exp.Next()) {
            hasSolids = true;
            ++partIndex;
            std::string name = "Part_" + std::to_string(partIndex);
            result.entities.push_back(doc.addShape(exp.Current(), std::move(name)));
        }

        if (!hasSolids) {
            for (TopExp_Explorer exp(shape, TopAbs_SHELL); exp.More(); exp.Next()) {
                ++partIndex;
                std::string name = "Shell_" + std::to_string(partIndex);
                result.entities.push_back(doc.addShape(exp.Current(), std::move(name)));
            }

            if (partIndex == 0) {
                ++partIndex;
                result.entities.push_back(doc.addShape(shape, "Shape_1"));
            }
        }
    } else {
        ++partIndex;
        result.entities.push_back(doc.addShape(shape, "Shape_1"));
    }

    result.report.entityCount = result.entities.size();
    result.report.brepAssetCount = result.entities.size();
    return result;
}

} // anonymous namespace

core::Result<ImportResult> OCCTImporter::import(const std::string& path,
                     mulan::io::Document& doc,
                     const ImportOptions& /*options*/) {
    try {
        NumericLocaleGuard locale;

        TopoDS_Shape shape = readFile(path);
        auto result = populateDocument(shape, doc);

        return result;
    } catch (const std::exception& e) {
        return std::unexpected(core::Error::make(core::ErrorCode::Io, e.what()));
    }
}

std::vector<std::string> OCCTImporter::supportedExtensions() const {
    return {"step", "stp", "iges", "igs"};
}

std::string OCCTImporter::name() const {
    return "OCCT Importer";
}

} // namespace mulan::io

