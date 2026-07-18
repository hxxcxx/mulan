#include <gtest/gtest.h>

#include <mulan/modeling/core/shape_file_reader.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/modeling/runtime/runtime.h>

namespace mulan::modeling {
namespace {

TEST(OcctPluginIntegrationTest, LoadsAndExecutesRealBackend) {
    runtime::init();

    auto& registry = ShapeOpsRegistry::instance();
    ASSERT_EQ(registry.selectedBackend(), "occt");
    IShapeOps* ops = registry.ops();
    ASSERT_NE(ops, nullptr);

    auto stepReader = ShapeFileReaderRegistry::instance().create("step");
    ASSERT_NE(stepReader, nullptr);
    EXPECT_EQ(stepReader->name(), "OCCT Importer");

    ExtrudeParams params;
    params.circleProfile = math::Circle3{ math::Point3::origin(), 2.0, math::Vec3::unitZ() };
    params.distance = 5.0;

    auto shape = ops->extrude(params);
    ASSERT_TRUE(shape.has_value()) << shape.error().message;
    EXPECT_TRUE(shape->valid());
    EXPECT_EQ(shape->bodyKind(), BodyKind::Solid);
    EXPECT_FALSE(shape->bounds().isEmpty());

    auto geometry = shape->tessellate();
    ASSERT_TRUE(geometry.has_value()) << geometry.error().message;
    EXPECT_FALSE(geometry->solidMesh.empty());
    EXPECT_GT(geometry->solidMesh.triangleCount(), 0u);
    EXPECT_FALSE(geometry->bounds.isEmpty());
}

}  // namespace
}  // namespace mulan::modeling
