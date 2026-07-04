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

        // glTF (fastgltf — 优先于 Assimp)
        factory.registerImporter("gltf", []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::GltfImporter>();
        });
        factory.registerImporter("glb", []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::GltfImporter>();
        });

        // Assimp (通用模型格式)
        auto assimp = []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::AssimpImporter>();
        };
        factory.registerImporter("obj",   assimp);
        factory.registerImporter("fbx",   assimp);
        factory.registerImporter("dae",   assimp);
        factory.registerImporter("3ds",   assimp);
        factory.registerImporter("ply",   assimp);
        factory.registerImporter("stl",   assimp);
        factory.registerImporter("blend", assimp);
        factory.registerImporter("x",     assimp);
        factory.registerImporter("ase",   assimp);
        factory.registerImporter("lwo",   assimp);
        factory.registerImporter("off",   assimp);
        factory.registerImporter("dxf",   assimp);

        // OCCT (CAD 格式)
        auto occt = []() -> std::unique_ptr<mulan::io::IFileImporter> {
            return std::make_unique<mulan::io::OCCTImporter>();
        };
        factory.registerImporter("step", occt);
        factory.registerImporter("stp",  occt);
        factory.registerImporter("iges", occt);
        factory.registerImporter("igs",  occt);
    }
};

static ImportRegistry _registry;

} // namespace
