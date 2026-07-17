#include <mulan/core/image/image.h>
#include <mulan/io/gltf_importer.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mulan::io {
namespace {

class TemporaryGltfDirectory {
public:
    TemporaryGltfDirectory() {
        path = std::filesystem::temp_directory_path() / "mulan_gltf_importer_tests";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TemporaryGltfDirectory() { std::filesystem::remove_all(path); }

    std::filesystem::path path;
};

std::vector<std::byte> trianglePositionBytes() {
    const std::array<float, 9> positions = {
        -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    std::vector<std::byte> bytes(sizeof(positions));
    std::memcpy(bytes.data(), positions.data(), sizeof(positions));
    return bytes;
}

std::vector<std::byte> makePngBytes(const std::filesystem::path& directory) {
    const auto path = directory / "texture.png";
    auto image = core::Image::createFromBuffer(1, 1, core::PixelFormat::RGBA8, { 255, 128, 0, 255 });
    EXPECT_TRUE(image->savePNG(path.string()));
    std::ifstream stream(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(stream)), {});
    std::vector<std::byte> bytes(chars.size());
    if (!chars.empty())
        std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

void appendU32(std::ofstream& stream, uint32_t value) {
    const std::array<char, 4> bytes = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu),
    };
    stream.write(bytes.data(), bytes.size());
}

void writeGlb(const std::filesystem::path& path, std::string json, std::vector<std::byte> binary) {
    while (json.size() % 4u != 0u)
        json.push_back(' ');
    while (binary.size() % 4u != 0u)
        binary.push_back(std::byte{ 0 });

    std::ofstream stream(path, std::ios::binary);
    appendU32(stream, 0x46546c67u);
    appendU32(stream, 2u);
    appendU32(stream, static_cast<uint32_t>(12u + 8u + json.size() + 8u + binary.size()));
    appendU32(stream, static_cast<uint32_t>(json.size()));
    appendU32(stream, 0x4e4f534au);
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    appendU32(stream, static_cast<uint32_t>(binary.size()));
    appendU32(stream, 0x004e4942u);
    stream.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
}

std::string triangleSceneJson(std::string_view bufferSource, size_t bufferBytes,
                              std::optional<std::pair<size_t, size_t>> imageRange = std::nullopt) {
    std::ostringstream json;
    json << R"({"asset":{"version":"2.0"},"buffers":[{)";
    if (!bufferSource.empty())
        json << bufferSource << ',';
    json << R"("byteLength":)" << bufferBytes << R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36})";
    if (imageRange)
        json << R"(,{"buffer":0,"byteOffset":)" << imageRange->first << R"(,"byteLength":)" << imageRange->second
             << '}';
    json << R"(],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[1,1,0]}],)";
    if (imageRange) {
        json << R"("images":[{"bufferView":1,"mimeType":"image/png"}],"textures":[{"source":0}],)"
             << R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)";
    }
    json << R"("meshes":[{"primitives":[{"attributes":{"POSITION":0})";
    if (imageRange)
        json << R"(,"material":0)";
    json << R"(}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    return json.str();
}

TEST(GltfImporterTests, LoadsSelfContainedGlbFromResidentPreflightData) {
    TemporaryGltfDirectory files;
    std::vector<std::byte> binary = trianglePositionBytes();
    const std::vector<std::byte> png = makePngBytes(files.path);
    const size_t imageOffset = binary.size();
    binary.insert(binary.end(), png.begin(), png.end());

    const auto model = files.path / "embedded.glb";
    writeGlb(model, triangleSceneJson({}, binary.size(), std::pair{ imageOffset, png.size() }), std::move(binary));

    const auto result = GltfImporter{}.parse(model.string());
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->textures.size(), 1u);
    EXPECT_EQ(result->textures.front().data, png);
    ASSERT_EQ(result->meshes.size(), 1u);
    ASSERT_EQ(result->meshes.front().primitives.size(), 1u);
    EXPECT_EQ(result->meshes.front().primitives.front().mesh.vertexCount(), 3u);
    EXPECT_EQ(result->nodes.size(), 1u);
}

TEST(GltfImporterTests, LoadsValidatedExternalBufferThroughSecondStage) {
    TemporaryGltfDirectory files;
    const std::vector<std::byte> binary = trianglePositionBytes();
    const auto bufferPath = files.path / "mesh.bin";
    std::ofstream(bufferPath, std::ios::binary)
            .write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));

    const auto model = files.path / "external.gltf";
    std::ofstream(model, std::ios::binary) << triangleSceneJson(R"("uri":"mesh.bin")", binary.size());

    const auto result = GltfImporter{}.parse(model.string());
    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->meshes.size(), 1u);
    ASSERT_EQ(result->meshes.front().primitives.size(), 1u);
    EXPECT_EQ(result->meshes.front().primitives.front().mesh.vertexCount(), 3u);
}

}  // namespace
}  // namespace mulan::io
