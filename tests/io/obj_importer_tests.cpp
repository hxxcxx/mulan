#include <mulan/io/obj_importer.h>

#include <mulan/graphics/vertex/vertex_semantic.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace mulan::io {
namespace {

class TemporaryObjDirectory {
public:
    TemporaryObjDirectory() {
        static std::atomic_uint64_t sequence = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("mulan_obj_importer_tests_" + std::to_string(stamp) + "_" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path);
    }

    ~TemporaryObjDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::filesystem::path write(std::string_view relativePath, std::string_view content) const {
        const std::filesystem::path output = path / relativePath;
        std::filesystem::create_directories(output.parent_path());
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        return output;
    }

    std::filesystem::path path;
};

TEST(ObjImporterTests, ImportsMaterialTexturesAndTriangulatesPolygon) {
    TemporaryObjDirectory files;
    files.write("textures/albedo.png", "albedo");
    files.write("textures/normal.png", "normal");
    files.write("textures/emissive.png", "emissive");
    files.write("materials/scene.mtl", R"(newmtl Painted
Kd 0.2 0.4 0.6
d 0.75
Pr 0.7
Pm 0.3
Ke 0.1 0.2 0.3
map_Kd ../textures/albedo.png
norm ../textures/normal.png
map_Ke ../textures/emissive.png
)");
    const auto obj = files.write("scene.obj", R"(mtllib materials/scene.mtl
o Quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
s 1
usemtl Painted
f 1/1 2/2 3/3 4/4
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->materials.size(), 1u);
    ASSERT_EQ(result->textures.size(), 3u);
    const ParsedMaterial& material = result->materials.front();
    EXPECT_NEAR(material.baseColorFactor.x, 0.2, 1.0e-6);
    EXPECT_NEAR(material.baseColorFactor.y, 0.4, 1.0e-6);
    EXPECT_NEAR(material.baseColorFactor.z, 0.6, 1.0e-6);
    EXPECT_NEAR(material.baseColorFactor.w, 0.75, 1.0e-6);
    EXPECT_NEAR(material.roughness, 0.7, 1.0e-6);
    EXPECT_NEAR(material.metallic, 0.3, 1.0e-6);
    EXPECT_EQ(material.shadingModel, graphics::MaterialShadingModel::MetallicRoughness);
    EXPECT_EQ(material.alphaMode, graphics::AlphaMode::Blend);
    EXPECT_LT(material.baseColorTexture, result->textures.size());
    EXPECT_LT(material.normalTexture, result->textures.size());
    EXPECT_LT(material.emissiveTexture, result->textures.size());
    EXPECT_EQ(result->textures[material.baseColorTexture].name, "albedo.png");
    EXPECT_EQ(result->textures[material.normalTexture].name, "normal.png");
    EXPECT_EQ(result->textures[material.emissiveTexture].name, "emissive.png");

    ASSERT_EQ(result->meshes.size(), 1u);
    ASSERT_EQ(result->meshes.front().primitives.size(), 1u);
    const graphics::Mesh& mesh = result->meshes.front().primitives.front().mesh;
    EXPECT_EQ(mesh.triangleCount(), 2u);
    EXPECT_NE(mesh.layout.offsetOf(graphics::VertexSemantic::Tangent), 0xFFFFu);
    ASSERT_EQ(result->nodes.size(), 2u);
    EXPECT_EQ(result->nodes[1].parent, 0u);
    EXPECT_EQ(result->nodes[1].meshIndex, 0u);
}

TEST(ObjImporterTests, IgnoresMinewaysSamplerMetadataWithoutDroppingMaterialLibrary) {
    TemporaryObjDirectory files;
    files.write("atlas.png", "atlas");
    files.write("scene.mtl", R"(newmtl Stone
Ns 0
Ka 0.1 0.1 0.1
Kd 0.5 0.5 0.5
Ks 0 0 0
interpolateMode NEAREST_MAGNIFICATION_TRILINEAR_MIPMAP_MINIFICATION
map_Ka atlas.png
map_Kd atlas.png
illum 2
)");
    const auto obj = files.write("scene.obj", R"(mtllib scene.mtl
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
usemtl Stone
f 1/1 2/2 3/3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->materials.size(), 1u);
    ASSERT_EQ(result->textures.size(), 1u);
    EXPECT_EQ(result->materials[0].baseColorTexture, 0u);
    EXPECT_EQ(result->materials[0].ambientTexture, 0u);
    EXPECT_EQ(result->materials[0].shadingModel, graphics::MaterialShadingModel::BlinnPhong);
}

TEST(ObjImporterTests, MapsIllumModelsWithoutConfusingEmissionWithUnlit) {
    TemporaryObjDirectory files;
    files.write("scene.mtl", R"(newmtl Constant
illum 0
Kd 1 0 0
newmtl Diffuse
illum 1
Kd 0 1 0
Ke 0.2 0.1 0.0
newmtl Specular
illum 2
Kd 0 0 1
Ks 0.4 0.5 0.6
Ns 48
)");
    const auto obj = files.write("scene.obj", R"(mtllib scene.mtl
v 0 0 0
v 1 0 0
v 0 1 0
usemtl Constant
f 1 2 3
usemtl Diffuse
f 1 2 3
usemtl Specular
f 1 2 3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->materials.size(), 3u);
    EXPECT_EQ(result->materials[0].shadingModel, graphics::MaterialShadingModel::Unlit);
    EXPECT_EQ(result->materials[1].shadingModel, graphics::MaterialShadingModel::Lambert);
    EXPECT_EQ(result->materials[2].shadingModel, graphics::MaterialShadingModel::BlinnPhong);
    EXPECT_GT(result->materials[1].emissiveFactor.x, 0.0);
    EXPECT_NEAR(result->materials[2].specularFactor.x, 0.4, 1.0e-6);
    EXPECT_NEAR(result->materials[2].specularFactor.y, 0.5, 1.0e-6);
    EXPECT_NEAR(result->materials[2].specularFactor.z, 0.6, 1.0e-6);
    EXPECT_DOUBLE_EQ(result->materials[2].shininess, 48.0);
}

TEST(ObjImporterTests, RecognizesBakedLightingMaterialSetWithoutChangingOrdinaryLambert) {
    TemporaryObjDirectory files;
    files.write("baked.jpg", "texture");
    files.write("scene.mtl", R"(newmtl BakedA
illum 1
Kd 1 1 1
Ks 0 0 0
Ke 0 0 0
map_Kd baked.jpg
map_Ke baked.jpg
newmtl BakedB
illum 1
Kd 1 1 1
Ks 0 0 0
Ke 0 0 0
map_Kd baked.jpg
map_Ke baked.jpg
newmtl ExporterTypo
illum 1
Kd 1 1 1
Ks 0 0 0
Ke 0 0 0
map_Kd baked.jpg
newmtl OrdinaryLambert
illum 1
Kd 0.5 0.5 0.5
Ks 0.1 0.1 0.1
map_Kd baked.jpg
)");
    const auto obj = files.write("scene.obj", R"(mtllib scene.mtl
v 0 0 0
v 1 0 0
v 0 1 0
usemtl BakedA
f 1 2 3
usemtl BakedB
f 1 2 3
usemtl ExporterTypo
f 1 2 3
usemtl OrdinaryLambert
f 1 2 3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->materials.size(), 4u);
    EXPECT_EQ(result->materials[0].shadingModel, graphics::MaterialShadingModel::Unlit);
    EXPECT_EQ(result->materials[1].shadingModel, graphics::MaterialShadingModel::Unlit);
    EXPECT_EQ(result->materials[2].shadingModel, graphics::MaterialShadingModel::Unlit);
    EXPECT_EQ(result->materials[3].shadingModel, graphics::MaterialShadingModel::Lambert);
    EXPECT_TRUE(result->materials[0].doubleSided);
    EXPECT_TRUE(result->materials[1].doubleSided);
    EXPECT_TRUE(result->materials[2].doubleSided);
    EXPECT_FALSE(result->materials[3].doubleSided);
}

TEST(ObjImporterTests, KeepsMaterialBoundariesAsSeparatePrimitives) {
    TemporaryObjDirectory files;
    files.write("scene.mtl", R"(newmtl Red
Kd 1 0 0
newmtl Blue
Kd 0 0 1
)");
    const auto obj = files.write("scene.obj", R"(mtllib scene.mtl
o TwoMaterials
v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
s off
usemtl Red
f 1 2 3
usemtl Blue
f 2 4 3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->materials.size(), 2u);
    ASSERT_EQ(result->meshes.size(), 1u);
    ASSERT_EQ(result->meshes.front().primitives.size(), 2u);
    ASSERT_EQ(result->meshes.front().materialIndices.size(), 2u);
    EXPECT_NE(result->meshes.front().materialIndices[0], result->meshes.front().materialIndices[1]);
    EXPECT_EQ(result->meshes.front().primitives[0].mesh.triangleCount(), 1u);
    EXPECT_EQ(result->meshes.front().primitives[1].mesh.triangleCount(), 1u);
}

TEST(ObjImporterTests, HonorsSmoothingGroupsWhenGeneratingMissingNormals) {
    TemporaryObjDirectory files;
    const auto obj = files.write("scene.obj", R"(o Smooth
v 0 0 0
v 1 0 0
v 0 1 0
v 0 0 1
s 1
f 1 2 3
f 1 4 2
o Flat
s off
f 1 2 3
f 1 4 2
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->meshes.size(), 2u);
    EXPECT_EQ(result->meshes[0].name, "Smooth");
    EXPECT_EQ(result->meshes[1].name, "Flat");
    ASSERT_EQ(result->meshes[0].primitives.size(), 1u);
    ASSERT_EQ(result->meshes[1].primitives.size(), 1u);
    EXPECT_EQ(result->meshes[0].primitives[0].mesh.vertexCount(), 4u);
    EXPECT_EQ(result->meshes[1].primitives[0].mesh.vertexCount(), 6u);
}

TEST(ObjImporterTests, ConvertsBottomLeftTextureCoordinatesToCanonicalTopLeftOrigin) {
    TemporaryObjDirectory files;
    const auto obj = files.write("scene.obj", R"(v 0 0 0
v 1 0 0
v 0 1 0
vt 0.25 0.75
vt 1.25 1.75
vt 0.5 0.5
f 1/1 2/2 3/3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(result->meshes.size(), 1u);
    ASSERT_EQ(result->meshes.front().primitives.size(), 1u);
    const graphics::Mesh& mesh = result->meshes.front().primitives.front().mesh;
    ASSERT_EQ(mesh.vertexCount(), 3u);
    const uint16_t offset = mesh.layout.offsetOf(graphics::VertexSemantic::TexCoord0);
    ASSERT_NE(offset, 0xFFFFu);

    const auto readTexcoord = [&mesh, offset](size_t vertex) {
        float value[2]{};
        std::memcpy(value, mesh.vertices.data() + vertex * mesh.vertexStride() + offset, sizeof(value));
        return math::FVec2(value[0], value[1]);
    };
    const math::FVec2 first = readTexcoord(0);
    const math::FVec2 tiled = readTexcoord(1);
    EXPECT_FLOAT_EQ(first.x, 0.25f);
    EXPECT_FLOAT_EQ(first.y, 0.25f);
    EXPECT_FLOAT_EQ(tiled.x, 1.25f);
    EXPECT_FLOAT_EQ(tiled.y, -0.75f);
}

TEST(ObjImporterTests, RejectsMaterialLibraryOutsideModelDirectory) {
    TemporaryObjDirectory files;
    files.write("outside.mtl", "newmtl Outside\n");
    const auto obj = files.write("model/scene.obj", R"(mtllib ../outside.mtl
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("escapes the model directory"), std::string::npos);
}

TEST(ObjImporterTests, RejectsTextureOutsideModelDirectory) {
    TemporaryObjDirectory files;
    files.write("outside.png", "outside");
    files.write("model/materials/scene.mtl", R"(newmtl Unsafe
Kd 1 1 1
map_Kd ../../outside.png
)");
    const auto obj = files.write("model/scene.obj", R"(mtllib materials/scene.mtl
v 0 0 0
v 1 0 0
v 0 1 0
usemtl Unsafe
f 1 2 3
)");

    const auto result = ObjImporter{}.parse(obj.string());

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("texture escapes the model directory"), std::string::npos);
}

TEST(ObjImporterTests, AppliesOutputByteLimitAcrossAllShapes) {
    TemporaryObjDirectory files;
    const auto obj = files.write("scene.obj", R"(o First
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
o Second
f 1 3 2
)");
    ImportOptions options;
    options.maxTotalAccessorBytes = 150;

    const auto result = ObjImporter{}.parse(obj.string(), options);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("output exceeds import byte limits"), std::string::npos);
}

TEST(ObjImporterTests, RejectsSceneWhenNodeLimitCannotContainRoot) {
    TemporaryObjDirectory files;
    const auto obj = files.write("scene.obj", R"(v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)");
    ImportOptions options;
    options.maxNodeCount = 0;

    const auto result = ObjImporter{}.parse(obj.string(), options);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("node count exceeds import limits"), std::string::npos);
}

}  // namespace
}  // namespace mulan::io
