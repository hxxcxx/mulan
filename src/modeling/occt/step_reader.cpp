#include "step_reader.h"
#include "shape_factory.h"

#include <mulan/core/result/error.h>

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Interface_Static.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopAbs.hxx>

#include <cctype>
#include <clocale>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace mulan::modeling {
namespace {

/// STEP/IGES 解析器对数值 locale 敏感，读取期间强制 LC_NUMERIC=C。
class NumericLocaleGuard {
public:
    NumericLocaleGuard() : old_(std::setlocale(LC_NUMERIC, nullptr) ? std::setlocale(LC_NUMERIC, nullptr) : "") {
        std::setlocale(LC_NUMERIC, "C");
    }
    ~NumericLocaleGuard() {
        if (!old_.empty())
            std::setlocale(LC_NUMERIC, old_.c_str());
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
    if (status != IFSelect_RetDone)
        throw std::runtime_error("Failed to read STEP file: " + path);

    reader.TransferRoots();
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull())
        throw std::runtime_error("No valid shape found in STEP file");
    return shape;
}

TopoDS_Shape readIGES(const std::string& path) {
    IGESControl_Reader reader;
    IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());
    if (status != IFSelect_RetDone)
        throw std::runtime_error("Failed to read IGES file: " + path);

    reader.TransferRoots();
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull())
        throw std::runtime_error("No valid shape found in IGES file");
    return shape;
}

TopoDS_Shape readFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".step" || ext == ".stp")
        return readSTEP(path);
    if (ext == ".iges" || ext == ".igs")
        return readIGES(path);

    throw std::runtime_error("Unsupported format: " + ext);
}

/// 实体分级：solid -> shell -> whole，产出 NamedShape。
std::vector<NamedShape> splitIntoNamedShapes(const TopoDS_Shape& shape) {
    std::vector<NamedShape> result;
    int partIndex = 0;

    if (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID) {
        bool hasSolids = false;
        for (TopExp_Explorer exp(shape, TopAbs_SOLID); exp.More(); exp.Next()) {
            hasSolids = true;
            ++partIndex;
            result.push_back({ "Part_" + std::to_string(partIndex), makeShape(exp.Current()) });
        }

        if (!hasSolids) {
            for (TopExp_Explorer exp(shape, TopAbs_SHELL); exp.More(); exp.Next()) {
                ++partIndex;
                result.push_back({ "Shell_" + std::to_string(partIndex), makeShape(exp.Current()) });
            }
            if (partIndex == 0) {
                ++partIndex;
                result.push_back({ "Shape_1", makeShape(shape) });
            }
        }
    } else {
        result.push_back({ "Shape_1", makeShape(shape) });
    }

    return result;
}

}  // namespace

core::Result<std::vector<NamedShape>> OccStepReader::read(const std::string& path) {
    try {
        NumericLocaleGuard locale;
        TopoDS_Shape shape = readFile(path);
        return splitIntoNamedShapes(shape);
    } catch (const std::exception& e) {
        return std::unexpected(core::Error::make(core::ErrorCode::Io, e.what()));
    }
}

std::vector<std::string> OccStepReader::supportedExtensions() const {
    return { "step", "stp", "iges", "igs" };
}

std::string OccStepReader::name() const {
    return "OCCT Importer";
}

void registerOccStepReader() {
    ShapeFileReaderRegistry::instance().registerReader(
            []() -> std::unique_ptr<IShapeFileReader> { return std::make_unique<OccStepReader>(); });
}

}  // namespace mulan::modeling
