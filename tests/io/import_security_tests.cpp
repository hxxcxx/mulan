#include <mulan/io/gltf_importer.h>
#include <mulan/io/file_manager.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace mulan::io {
namespace {

TEST(BuiltinImporterRegistrationTests, FileManagerExposesEveryBuiltinImporterWithoutWholeArchiveLinking) {
    FileManager manager;
    const std::vector<std::string> extensions = manager.supportedExtensions();
    for (const std::string_view expected : { "gltf", "glb", "obj", "fbx", "stl" }) {
        EXPECT_NE(std::find(extensions.begin(), extensions.end(), expected), extensions.end()) << expected;
    }
}

class TemporaryImportDirectory {
public:
    TemporaryImportDirectory() {
        path = std::filesystem::temp_directory_path() / "mulan_import_security_tests";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path / "model");
    }
    ~TemporaryImportDirectory() { std::filesystem::remove_all(path); }

    std::filesystem::path writeModel(std::string_view json) const {
        const auto model = path / "model" / "scene.gltf";
        std::ofstream stream(model, std::ios::binary);
        stream << json;
        return model;
    }

    std::filesystem::path path;
};

TEST(GltfImportSecurityTests, RejectsExternalResourceOutsideModelDirectory) {
    TemporaryImportDirectory files;
    std::ofstream(files.path / "outside.bin", std::ios::binary).write("data", 4);
    const auto model =
            files.writeModel(R"({"asset":{"version":"2.0"},"buffers":[{"uri":"../outside.bin","byteLength":4}]})");

    const auto result = GltfImporter{}.parse(model.string());
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("escapes the model directory"), std::string::npos);
}

TEST(GltfImportSecurityTests, RejectsAccessorBeforeAllocatingItsDeclaredElementCount) {
    TemporaryImportDirectory files;
    const auto model = files.writeModel(
            R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":50000001,"type":"VEC3"}]})");

    const auto result = GltfImporter{}.parse(model.string());
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("accessor element count"), std::string::npos);
}

}  // namespace
}  // namespace mulan::io
