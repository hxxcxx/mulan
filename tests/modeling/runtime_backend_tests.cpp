#include <gtest/gtest.h>

#include <mulan/modeling/core/shape_file_reader.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/modeling/runtime/runtime.h>

#include <algorithm>
#include <cstdlib>

#ifndef MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND
#define MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND "occt"
#endif

namespace mulan::runtime {
namespace {

TEST(ModelingRuntimeTest, ConfiguresShapeOpsWithoutChangingOcctFileReader) {
#ifdef _WIN32
    ASSERT_EQ(_putenv_s("MULAN_SHAPE_OPS_BACKEND", ""), 0);
    ASSERT_EQ(_putenv_s("MULAN_MODELING_BACKEND", ""), 0);
#endif

    init();

    auto& opsRegistry = modeling::ShapeOpsRegistry::instance();
    EXPECT_EQ(opsRegistry.selectedBackend(), MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND);
    EXPECT_NE(opsRegistry.ops(), nullptr);

    const auto backends = opsRegistry.availableBackends();
    EXPECT_NE(std::find(backends.begin(), backends.end(), "occt"), backends.end());
    EXPECT_NE(std::find(backends.begin(), backends.end(), "truck"), backends.end());

    auto reader = modeling::ShapeFileReaderRegistry::instance().create("step");
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(reader->name(), "OCCT Importer");

#ifdef _WIN32
    ASSERT_EQ(_putenv_s("MULAN_SHAPE_OPS_BACKEND", "truck"), 0);
#endif

    init();

    EXPECT_EQ(opsRegistry.selectedBackend(), "truck");
    EXPECT_NE(opsRegistry.ops(), nullptr);

    reader = modeling::ShapeFileReaderRegistry::instance().create("step");
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(reader->name(), "OCCT Importer");

#ifdef _WIN32
    EXPECT_EQ(_putenv_s("MULAN_SHAPE_OPS_BACKEND", ""), 0);
#endif
}

}  // namespace
}  // namespace mulan::runtime
