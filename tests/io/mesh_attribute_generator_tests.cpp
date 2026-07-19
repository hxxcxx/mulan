#include "detail/mesh_attribute_generator.h"

#include <gtest/gtest.h>

#include <cmath>

namespace mulan::io::detail {
namespace {

TEST(MeshAttributeGeneratorTests, GeneratesFlatNormalsBySplittingFaceCorners) {
    TriangleMeshData mesh;
    mesh.positions = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
    };
    mesh.indices = { 0, 1, 2, 0, 3, 1 };
    mesh.tangents.resize(mesh.positions.size(), math::FVec4(1.0f, 0.0f, 0.0f, 1.0f));

    const auto result = generateFlatNormals(mesh);

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(mesh.positions.size(), 6u);
    ASSERT_EQ(mesh.normals.size(), 6u);
    EXPECT_TRUE(mesh.tangents.empty());
    EXPECT_EQ(mesh.indices, (std::vector<uint32_t>{ 0, 1, 2, 3, 4, 5 }));
    for (size_t index = 0; index < 3u; ++index)
        EXPECT_GT(mesh.normals[index].dot(math::FVec3::unitZ()), 0.9999f);
    for (size_t index = 3; index < 6u; ++index)
        EXPECT_GT(mesh.normals[index].dot(math::FVec3::unitY()), 0.9999f);
}

TEST(MeshAttributeGeneratorTests, GeneratesStableMikkTangentsForIndexedQuad) {
    TriangleMeshData mesh;
    mesh.positions = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
    };
    mesh.normals.resize(4u, math::FVec3::unitZ());
    mesh.texcoords = {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 1.0f, 1.0f },
        { 0.0f, 1.0f },
    };
    mesh.indices = { 0, 1, 2, 0, 2, 3 };

    const auto result = generateMikkTangents(mesh);

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(mesh.tangents.size(), mesh.positions.size());
    EXPECT_EQ(mesh.positions.size(), 4u);
    for (const auto& tangent : mesh.tangents) {
        EXPECT_GT(tangent.xyz().dot(math::FVec3::unitX()), 0.9999f);
        EXPECT_FLOAT_EQ(std::abs(tangent.w), 1.0f);
        EXPECT_NEAR(tangent.xyz().dot(math::FVec3::unitZ()), 0.0f, 1.0e-6f);
    }
}

TEST(MeshAttributeGeneratorTests, SplitsOnlyTheSharedVertexAtMirroredUvDiscontinuity) {
    TriangleMeshData mesh;
    mesh.positions = {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
    };
    mesh.normals.resize(5u, math::FVec3::unitZ());
    mesh.texcoords = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
    };
    mesh.indices = { 0, 1, 2, 0, 4, 3 };

    const auto result = generateMikkTangents(mesh);

    ASSERT_TRUE(result) << result.error().message;
    ASSERT_EQ(mesh.indices.size(), 6u);
    ASSERT_EQ(mesh.positions.size(), 6u);
    const uint32_t firstSharedVariant = mesh.indices[0];
    const uint32_t secondSharedVariant = mesh.indices[3];
    EXPECT_NE(firstSharedVariant, secondSharedVariant);
    EXPECT_EQ(mesh.tangents[firstSharedVariant].w, -mesh.tangents[secondSharedVariant].w);
}

TEST(MeshAttributeGeneratorTests, RejectsOutOfRangeIndicesWithoutMutatingMesh) {
    TriangleMeshData mesh;
    mesh.positions = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
    };
    mesh.normals.resize(3u, math::FVec3::unitZ());
    mesh.texcoords.resize(3u, math::FVec2(0.0f));
    mesh.indices = { 0, 1, 9 };
    const auto originalPositions = mesh.positions;

    const auto result = generateMikkTangents(mesh);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, static_cast<int32_t>(ErrorCode::InvalidArg));
    EXPECT_EQ(result.error().message, "invalid triangle-list indices");
    EXPECT_EQ(mesh.positions, originalPositions);
    EXPECT_TRUE(mesh.tangents.empty());
}

}  // namespace
}  // namespace mulan::io::detail
