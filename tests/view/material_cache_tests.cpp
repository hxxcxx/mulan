/**
 * @file material_cache_tests.cpp
 * @brief 验证材质缓存的大容量、稳定句柄与资产域清理语义。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/render/material/material_cache.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>

namespace mulan::engine {
namespace {

TEST(MaterialCacheTests, AcceptsMoreMaterialsThanLegacy256Limit) {
    MaterialCache cache;
    constexpr size_t kAddedMaterialCount = 512;

    MaterialHandle lastHandle = kInvalidMaterialHandle;
    for (size_t i = 0; i < kAddedMaterialCount; ++i) {
        Material material = Material::defaultPBR();
        material.roughness = static_cast<double>(i % 100u) / 100.0;
        lastHandle = cache.registerMaterial("asset-material:" + std::to_string(i), std::move(material));
        ASSERT_NE(lastHandle, kInvalidMaterialHandle) << "material index: " << i;
    }

    EXPECT_GT(cache.size(), 256u);
    EXPECT_EQ(cache.size(), kAddedMaterialCount + 3u);
    EXPECT_NE(cache.find(lastHandle), nullptr);
    EXPECT_NE(kInvalidMaterialHandle, MaterialHandle{ 0 });
}

TEST(MaterialCacheTests, SameResourceNameKeepsHandleAndUpdatesValue) {
    MaterialCache cache;
    Material first = Material::defaultPBR();
    first.roughness = 0.25;
    const MaterialHandle firstHandle = cache.registerMaterial("render-material:42", std::move(first));
    ASSERT_NE(firstHandle, kInvalidMaterialHandle);
    const size_t sizeAfterFirstRegistration = cache.size();

    Material updated = Material::defaultPBR();
    updated.roughness = 0.75;
    const MaterialHandle updatedHandle = cache.registerMaterial("render-material:42", std::move(updated));

    EXPECT_EQ(updatedHandle, firstHandle);
    EXPECT_EQ(cache.size(), sizeAfterFirstRegistration);
    ASSERT_NE(cache.find(firstHandle), nullptr);
    EXPECT_DOUBLE_EQ(cache.find(firstHandle)->roughness, 0.75);
}

TEST(MaterialCacheTests, ClearDropsAssetDomainMaterialsAndPreservesBuiltins) {
    MaterialCache cache;
    ASSERT_NE(cache.registerMaterial("render-material:1", Material::defaultPBR()), kInvalidMaterialHandle);
    ASSERT_NE(cache.registerMaterial("render-material:2", Material::defaultPBR()), kInvalidMaterialHandle);
    ASSERT_EQ(cache.size(), 5u);

    cache.clear();

    EXPECT_EQ(cache.size(), 3u);
    EXPECT_NE(cache.findByName("DefaultPBR"), nullptr);
    EXPECT_NE(cache.findByName("DefaultPhong"), nullptr);
    EXPECT_NE(cache.findByName("Wireframe"), nullptr);
    EXPECT_EQ(cache.findByName("render-material:1"), nullptr);
    EXPECT_EQ(cache.findByName("render-material:2"), nullptr);
    EXPECT_EQ(cache.registerMaterial("render-material:1", Material::defaultPBR()), MaterialHandle{ 3 });
}

}  // namespace
}  // namespace mulan::engine
