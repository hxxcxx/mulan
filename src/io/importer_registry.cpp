#include "importer_factory.h"
#include "detail/importer_registry.h"
#include "gltf_importer.h"
#include "obj_importer.h"
#include "assimp_importer.h"

#include <mutex>

namespace mulan::io::detail {

void ensureBuiltinImportersRegistered() {
    static std::once_flag once;
    std::call_once(once, []() {
        auto& factory = ImporterFactory::instance();

        // glTF (fastgltf)
        factory.registerImporter("gltf",
                                 []() -> std::unique_ptr<IFileImporter> { return std::make_unique<GltfImporter>(); });
        factory.registerImporter("glb",
                                 []() -> std::unique_ptr<IFileImporter> { return std::make_unique<GltfImporter>(); });

        // Wavefront OBJ 由 RapidObj 专门处理。
        factory.registerImporter("obj",
                                 []() -> std::unique_ptr<IFileImporter> { return std::make_unique<ObjImporter>(); });

        // Assimp
        auto assimp = []() -> std::unique_ptr<IFileImporter> {
            return std::make_unique<AssimpImporter>();
        };
        for (auto ext : { "fbx", "dae", "3ds", "ply", "stl", "blend", "x", "ase", "lwo", "off", "dxf" })
            factory.registerImporter(ext, assimp);
    });
}

}  // namespace mulan::io::detail
