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

TEST(MaterialCacheTests, RevisionChangesOnlyForRealRegistrationAndUpdates) {
    MaterialCache cache;
    EXPECT_NE(cache.revision(), 0u);
    const uint64_t initialLayoutRevision = cache.layoutRevision();

    const uint64_t initialRevision = cache.revision();
    Material material = Material::defaultPBR();
    material.roughness = 0.25;
    const MaterialHandle handle = cache.registerMaterial("revision-material", material);
    ASSERT_NE(handle, kInvalidMaterialHandle);
    EXPECT_EQ(cache.revision(), initialRevision + 1);

    const uint64_t addedRevision = cache.revision();
    EXPECT_EQ(cache.registerMaterial("revision-material", material), handle);
    EXPECT_EQ(cache.revision(), addedRevision);

    material.roughness = 0.75;
    EXPECT_EQ(cache.registerMaterial("revision-material", material), handle);
    EXPECT_EQ(cache.revision(), addedRevision + 1);

    const uint64_t replacedRevision = cache.revision();
    ASSERT_NE(cache.find(handle), nullptr);
    const Material current = *cache.find(handle);
    EXPECT_TRUE(cache.updateMaterial(handle, current));
    EXPECT_TRUE(cache.updateMaterial("revision-material", current));
    EXPECT_EQ(cache.revision(), replacedRevision);

    Material updated = current;
    updated.metallic = 0.5;
    EXPECT_TRUE(cache.updateMaterial(handle, updated));
    EXPECT_EQ(cache.revision(), replacedRevision + 1);

    const uint64_t updatedRevision = cache.revision();
    EXPECT_FALSE(cache.updateMaterial(kInvalidMaterialHandle, updated));
    EXPECT_FALSE(cache.updateMaterial("missing-material", updated));
    EXPECT_EQ(cache.revision(), updatedRevision);
    EXPECT_EQ(cache.layoutRevision(), initialLayoutRevision);
}

TEST(MaterialCacheTests, RevisionChangesOnlyForSuccessfulRemoveAndNonEmptyClear) {
    MaterialCache cache;
    const uint64_t initialRevision = cache.revision();

    EXPECT_FALSE(cache.remove(0));
    EXPECT_FALSE(cache.remove(kInvalidMaterialHandle));
    cache.clear();
    EXPECT_EQ(cache.revision(), initialRevision);

    const MaterialHandle first = cache.registerMaterial("revision-clear-1", Material::defaultPBR());
    ASSERT_NE(first, kInvalidMaterialHandle);
    const MaterialHandle second = cache.registerMaterial("revision-clear-2", Material::defaultPBR());
    ASSERT_NE(second, kInvalidMaterialHandle);

    const uint64_t beforeRemove = cache.revision();
    const uint64_t layoutBeforeRemove = cache.layoutRevision();
    EXPECT_TRUE(cache.remove(first));
    EXPECT_EQ(cache.revision(), beforeRemove + 1);
    EXPECT_EQ(cache.layoutRevision(), layoutBeforeRemove + 1);

    const uint64_t beforeClear = cache.revision();
    const uint64_t layoutBeforeClear = cache.layoutRevision();
    cache.clear();
    EXPECT_EQ(cache.size(), 3u);
    EXPECT_EQ(cache.revision(), beforeClear + 1);
    EXPECT_EQ(cache.layoutRevision(), layoutBeforeClear + 1);

    const uint64_t clearedRevision = cache.revision();
    cache.clear();
    EXPECT_EQ(cache.revision(), clearedRevision);
    EXPECT_EQ(cache.layoutRevision(), layoutBeforeClear + 1);
}

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
