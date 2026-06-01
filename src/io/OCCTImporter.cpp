/**
 * @file OCCTImporter.cpp
 * @brief OCCT 文件导入器实现 — 直接填充 World
 * @author hxxcxx
 * @date 2026-04-22
 */
#include "OCCTImporter.h"
#include "ImporterFactory.h"

#include <mulan/world/World.h>
#include <mulan/world/Entity.h>
#include <mulan/world/geometry/SolidGeometryData.h>

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <Interface_Static.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <clocale>
#include <filesystem>

namespace mulan::io {

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

void populateWorld(const TopoDS_Shape& shape, world::World& world) {
    int partIndex = 0;

    if (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID) {
        bool hasSolids = false;
        for (TopExp_Explorer exp(shape, TopAbs_SOLID); exp.More(); exp.Next()) {
            hasSolids = true;
            ++partIndex;
            std::string name = "Part_" + std::to_string(partIndex);
            auto* entity = world.createEntity(std::move(name));
            auto geo = std::make_unique<world::SolidGeometryData>(exp.Current());
            entity->setGeometry(std::move(geo));
        }

        if (!hasSolids) {
            for (TopExp_Explorer exp(shape, TopAbs_SHELL); exp.More(); exp.Next()) {
                ++partIndex;
                std::string name = "Shell_" + std::to_string(partIndex);
                auto* entity = world.createEntity(std::move(name));
                auto geo = std::make_unique<world::SolidGeometryData>(exp.Current());
                entity->setGeometry(std::move(geo));
            }

            if (partIndex == 0) {
                ++partIndex;
                auto* entity = world.createEntity("Shape_1");
                auto geo = std::make_unique<world::SolidGeometryData>(shape);
                entity->setGeometry(std::move(geo));
            }
        }
    } else {
        ++partIndex;
        auto* entity = world.createEntity("Shape_1");
        auto geo = std::make_unique<world::SolidGeometryData>(shape);
        entity->setGeometry(std::move(geo));
    }
}

} // anonymous namespace

bool OCCTImporter::import(const std::string& path, mulan::world::World& world) {
    try {
        auto* oldLocale = std::setlocale(LC_NUMERIC, "C");

        TopoDS_Shape shape = readFile(path);
        populateWorld(shape, world);

        if (oldLocale) std::setlocale(LC_NUMERIC, oldLocale);
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
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

} // namespace mulan::io
