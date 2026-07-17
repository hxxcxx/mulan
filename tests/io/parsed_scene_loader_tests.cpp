#include <mulan/asset/asset_library.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/core/image/image.h>
#include <mulan/io/document.h>
#include <mulan/io/import_builder.h>
#include <mulan/io/parsed_scene_loader.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace mulan::io {
namespace {

std::filesystem::path writeTestPng(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::vector<uint8_t> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    };
    auto image = core::Image::createFromBuffer(2, 2, core::PixelFormat::RGBA8, std::move(pixels));
    EXPECT_TRUE(image->savePNG(path.string()));
    return path;
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(stream)), {});
    std::vector<std::byte> bytes(chars.size());
    if (!chars.empty())
        std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

graphics::Mesh makeTriangleMesh() {
    const std::array<math::FVec3, 3> positions = {
        math::FVec3{ -1.0f, 0.0f, 0.0f },
        math::FVec3{ 2.0f, 0.0f, 0.0f },
        math::FVec3{ 0.0f, 3.0f, 0.0f },
    };
    const std::array<math::FVec3, 3> normals = {
        math::FVec3::unitZ(),
        math::FVec3::unitZ(),
        math::FVec3::unitZ(),
    };
    const std::array<math::FVec2, 3> texcoords = {
        math::FVec2{ 0.0f, 0.0f },
        math::FVec2{ 1.0f, 0.0f },
        math::FVec2{ 0.0f, 1.0f },
    };
    const std::array<uint32_t, 3> indices = { 0, 1, 2 };
    return buildStandardMesh(StandardMeshSource{
            .positions = positions,
            .normals = normals,
            .texcoords = texcoords,
            .indices = indices,
    });
}

TEST(ParsedSceneLoaderTests, MovesEmbeddedTextureAndMeshIntoDocumentWithoutChangingContent) {
    const auto pngPath = writeTestPng("mulan_parsed_scene_loader_embedded.png");
    const std::vector<std::byte> encoded = readBytes(pngPath);
    std::filesystem::remove(pngPath);
    ASSERT_FALSE(encoded.empty());

    ParsedScene scene;
    scene.textures.push_back(ParsedTexture{
            .name = "Embedded",
            .sourcePath = "gltf:test#image[0]",
            .data = encoded,
            .mimeType = "image/png",
    });
    scene.materials.push_back(ParsedMaterial{
            .name = "Material",
            .baseColorTexture = 0,
    });

    graphics::Mesh sourceMesh = makeTriangleMesh();
    const std::vector<std::byte> expectedVertices = sourceMesh.vertices;
    const std::vector<std::byte> expectedIndices = sourceMesh.indices;
    const math::AABB3 expectedBounds = sourceMesh.bounds;
    ParsedMesh parsedMesh;
    parsedMesh.name = "Triangle";
    parsedMesh.primitives.push_back(asset::MeshPrimitive{ .mesh = std::move(sourceMesh), .name = "Primitive" });
    parsedMesh.materialIndices.push_back(0);
    scene.meshes.push_back(std::move(parsedMesh));
    scene.nodes.push_back(ParsedNode{ .name = "Root", .meshIndex = 0 });
    scene.rootNodes.push_back(0);

    Document document("LoaderTest");
    ParsedSceneLoader loader(document);
    const ImportResult result = loader.load(std::move(scene));

    EXPECT_EQ(result.report.textureCount, 1u);
    EXPECT_EQ(result.report.materialCount, 1u);
    EXPECT_EQ(result.report.meshAssetCount, 1u);
    EXPECT_EQ(result.report.primitiveCount, 1u);
    EXPECT_EQ(result.report.entityCount, 1u);
    EXPECT_TRUE(result.report.warnings.empty());

    const asset::TextureAsset* texture = nullptr;
    const asset::MaterialAsset* material = nullptr;
    const asset::MeshAsset* mesh = nullptr;
    document.assets()->forEachAsset([&](const asset::Asset& value) {
        if (value.kind() == asset::AssetKind::Texture)
            texture = static_cast<const asset::TextureAsset*>(&value);
        else if (value.kind() == asset::AssetKind::Material)
            material = static_cast<const asset::MaterialAsset*>(&value);
        else if (value.kind() == asset::AssetKind::Mesh)
            mesh = static_cast<const asset::MeshAsset*>(&value);
    });

    ASSERT_NE(texture, nullptr);
    ASSERT_TRUE(texture->hasImage());
    EXPECT_EQ(texture->embeddedBytes(), encoded);
    EXPECT_EQ(texture->mimeType(), "image/png");
    EXPECT_EQ(texture->width(), 2);
    EXPECT_EQ(texture->height(), 2);

    ASSERT_NE(material, nullptr);
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->primitiveCount(), 1u);
    const auto& primitive = mesh->primitives().front();
    EXPECT_EQ(primitive.material, material->id());
    EXPECT_EQ(primitive.name, "Primitive");
    EXPECT_EQ(primitive.mesh.vertices, expectedVertices);
    EXPECT_EQ(primitive.mesh.indices, expectedIndices);
    EXPECT_DOUBLE_EQ(primitive.mesh.bounds.min.x, expectedBounds.min.x);
    EXPECT_DOUBLE_EQ(primitive.mesh.bounds.min.y, expectedBounds.min.y);
    EXPECT_DOUBLE_EQ(primitive.mesh.bounds.max.x, expectedBounds.max.x);
    EXPECT_DOUBLE_EQ(primitive.mesh.bounds.max.y, expectedBounds.max.y);
}

TEST(ParsedSceneLoaderTests, ReadsExternalTextureOnceAndKeepsItsEncodedSource) {
    const auto pngPath = writeTestPng("mulan_parsed_scene_loader_external.png");
    const std::vector<std::byte> encoded = readBytes(pngPath);
    ASSERT_FALSE(encoded.empty());

    ParsedScene scene;
    scene.textures.push_back(ParsedTexture{
            .name = "External",
            .sourcePath = pngPath.string(),
            .mimeType = "image/png",
    });

    Document document("ExternalTextureTest");
    ParsedSceneLoader loader(document);
    const ImportResult result = loader.load(std::move(scene));
    std::filesystem::remove(pngPath);

    ASSERT_EQ(result.report.textureCount, 1u);
    EXPECT_TRUE(result.report.warnings.empty());
    const asset::TextureAsset* texture = nullptr;
    document.assets()->forEachAsset([&](const asset::Asset& value) {
        if (value.kind() == asset::AssetKind::Texture)
            texture = static_cast<const asset::TextureAsset*>(&value);
    });
    ASSERT_NE(texture, nullptr);
    EXPECT_TRUE(texture->hasImage());
    EXPECT_EQ(texture->embeddedBytes(), encoded);
}

}  // namespace
}  // namespace mulan::io
