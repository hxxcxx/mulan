/**
 * @file importer_registry.cpp
 * @brief 集中注册所有导入器到 ImporterFactory
 *
 * 新增格式只需在此文件加一行 registerImporter 即可。
 * 各 importer 类本身不携带注册逻辑，保持纯粹。
 */
#include "importer_factory.h"
#include "gltf_importer.h"
#include "assimp_importer.h"
#include "occt_importer.h"

namespace {

struct ImportRegistry {
    ImportRegistry() {
        auto& factory = mulan::io::ImporterFactory::instance();

        // glTF (fastgltf)
        factory.registerImporter("gltf", []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::GltfImporter>();
        });
        factory.registerImporter("glb", []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::GltfImporter>();
        });

        // Assimp
        auto assimp = []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::AssimpImporter>();
        };
        for (auto ext : {"obj","fbx","dae","3ds","ply","stl","blend","x","ase","lwo","off","dxf"})
            factory.registerImporter(ext, assimp);

        // OCCT
        auto occt = []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::OCCTImporter>();
        };
        for (auto ext : {"step","stp","iges","igs"})
            factory.registerImporter(ext, occt);
    }
};

static ImportRegistry _registry;

} // namespace
