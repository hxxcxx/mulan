/**
 * @file asset_revision_tests.cpp
 * @brief 资产内容版本、资产库成员版本与受控 mutator 契约测试
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <gtest/gtest.h>

#include <mulan/asset/asset_library.h>
#include <mulan/asset/curve_asset.h>
#include <mulan/asset/material_asset.h>
#include <mulan/asset/mesh_asset.h>
#include <mulan/asset/tessellated_asset.h>
#include <mulan/asset/texture_asset.h>
#include <mulan/graphics/mesh.h>
#include <mulan/math/math.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace mulan::asset {
namespace {

struct MutableAssetVisitor {
    void operator()(Asset&) const {}
};

template <typename Visitor>
concept AssetLibraryVisitor = requires(const AssetLibrary& library, Visitor visitor) { library.forEachAsset(visitor); };

static_assert(!AssetLibraryVisitor<MutableAssetVisitor>);

CurvePrimitive segment(double endX) {
    return CurvePrimitive::segment(math::Segment3{ math::Point3::origin(), math::Point3{ endX, 0.0, 0.0 } });
}

graphics::Mesh nonEmptyMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.vertices.resize(mesh.layout.stride());
    mesh.bounds = { math::Point3::origin(), math::Point3::origin() };
    return mesh;
}

TEST(AssetLibraryRevision, MembershipRevisionOnlyTracksRealCollectionChanges) {
    AssetLibrary library;
    EXPECT_EQ(library.membershipRevision(), 0u);
    EXPECT_FALSE(library.remove(AssetId{ 99 }));
    library.clear();
    EXPECT_EQ(library.membershipRevision(), 0u);

    MaterialAsset* material = library.create<MaterialAsset>("Material");
    ASSERT_NE(material, nullptr);
    const AssetId materialId = material->id();
    EXPECT_EQ(library.membershipRevision(), 1u);
    ASSERT_TRUE(library.contentRevision(materialId).has_value());
    EXPECT_EQ(*library.contentRevision(materialId), AssetRevision{ 1 });

    TextureAsset* texture = library.create<TextureAsset>("Texture");
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(library.membershipRevision(), 2u);

    size_t visited = 0;
    library.forEachAsset([&visited](const Asset&) { ++visited; });
    EXPECT_EQ(visited, 2u);

    EXPECT_TRUE(library.remove(materialId));
    EXPECT_EQ(library.membershipRevision(), 3u);
    EXPECT_FALSE(library.remove(materialId));
    EXPECT_EQ(library.membershipRevision(), 3u);

    library.clear();
    EXPECT_EQ(library.membershipRevision(), 4u);
    library.clear();
    EXPECT_EQ(library.membershipRevision(), 4u);
}

TEST(AssetContentRevision, MaterialAndTextureNoOpsDoNotPublish) {
    MaterialAsset material(AssetId{ 1 }, "Material");
    EXPECT_EQ(material.revision(), 1u);

    material.setRoughness(0.5);
    material.setBaseColor(math::Vec3{ 0.8, 0.8, 0.8 });
    material.setDoubleSided(false);
    EXPECT_EQ(material.revision(), 1u);

    material.setRoughness(0.25);
    EXPECT_EQ(material.revision(), 2u);
    material.setRoughness(0.25);
    EXPECT_EQ(material.revision(), 2u);
    material.setDoubleSided(true);
    EXPECT_EQ(material.revision(), 3u);

    TextureAsset texture(AssetId{ 2 }, "Texture", "source.png");
    texture.setSourcePath("source.png");
    texture.setSize(0, 0);
    EXPECT_EQ(texture.revision(), 1u);
    texture.setEmbeddedBytes(std::vector<std::byte>{ std::byte{ 1 } });
    EXPECT_EQ(texture.revision(), 2u);
    texture.setEmbeddedBytes(std::vector<std::byte>{ std::byte{ 1 } });
    EXPECT_EQ(texture.revision(), 2u);
}

TEST(AssetContentRevision, GeometryMutatorsPublishExactlyOncePerRealChange) {
    CurveAsset curve(AssetId{ 1 }, "Curve");
    const CurveElementId element = curve.add(segment(1.0));
    EXPECT_EQ(curve.revision(), 2u);
    EXPECT_TRUE(curve.update(element, segment(1.0)));
    EXPECT_EQ(curve.revision(), 2u);
    EXPECT_TRUE(curve.update(element, segment(2.0)));
    EXPECT_EQ(curve.revision(), 3u);
    curve.setElements(curve.elements());
    EXPECT_EQ(curve.revision(), 3u);
    EXPECT_TRUE(curve.remove(element));
    EXPECT_EQ(curve.revision(), 4u);
    EXPECT_FALSE(curve.remove(element));
    EXPECT_EQ(curve.revision(), 4u);

    MeshAsset mesh(AssetId{ 2 }, "Mesh");
    mesh.addPrimitive(nonEmptyMesh());
    EXPECT_EQ(mesh.revision(), 2u);

    TessellatedAsset tessellated(AssetId{ 3 }, "Tessellated");
    tessellated.setRenderMeshes({}, {});
    EXPECT_EQ(tessellated.revision(), 1u);
    const graphics::Mesh solid = nonEmptyMesh();
    tessellated.setRenderMeshes(solid, {});
    EXPECT_EQ(tessellated.revision(), 2u);
    tessellated.setRenderMeshes(solid, {});
    EXPECT_EQ(tessellated.revision(), 2u);
}

}  // namespace
}  // namespace mulan::asset
