#include "importer_factory.h"
#include "gltf_importer.h"
#include "assimp_importer.h"

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
        for (auto ext : { "obj", "fbx", "dae", "3ds", "ply", "stl", "blend", "x", "ase", "lwo", "off", "dxf" })
            factory.registerImporter(ext, assimp);
    }
};

static ImportRegistry _registry;

}  // namespace
