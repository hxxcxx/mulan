#include <gtest/gtest.h>

#include <mulan/modeling/core/shape_file_reader.h>
#include <mulan/modeling/core/shape_ops.h>
#include <mulan/modeling/runtime/runtime.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

#ifndef MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND
#define MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND "occt"
#endif

#ifndef MULAN_TEST_HAS_TRUCK_BACKEND
#define MULAN_TEST_HAS_TRUCK_BACKEND 0
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
#if MULAN_TEST_HAS_TRUCK_BACKEND
    EXPECT_NE(std::find(backends.begin(), backends.end(), "truck"), backends.end());
#else
    EXPECT_EQ(std::find(backends.begin(), backends.end(), "truck"), backends.end());
#endif

    auto reader = modeling::ShapeFileReaderRegistry::instance().create("step");
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(reader->name(), "OCCT Importer");

#ifdef _WIN32
#if MULAN_TEST_HAS_TRUCK_BACKEND
    const char* overrideBackend = std::string_view(MULAN_TEST_DEFAULT_SHAPE_OPS_BACKEND) == "truck" ? "occt" : "truck";
#else
    const char* overrideBackend = "occt";
#endif
    ASSERT_EQ(_putenv_s("MULAN_SHAPE_OPS_BACKEND", overrideBackend), 0);
#endif

    init();

#ifdef _WIN32
    EXPECT_EQ(opsRegistry.selectedBackend(), overrideBackend);
#endif
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
