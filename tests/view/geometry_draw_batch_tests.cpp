/**
 * @file geometry_draw_batch_tests.cpp
 * @brief 验证对象实例批的完整兼容键、阈值、容量拆分与安全回退。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include <mulan/render/draw/geometry_draw_batch.h>
#include <mulan/render/gpu_scene_contract.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mulan::engine {
namespace {

template <typename T>
T* fakePointer(uintptr_t value) {
    return reinterpret_cast<T*>(value);
}

MeshDrawCommand batchCommand() {
    MeshDrawCommand command;
    command.pipelineState = fakePointer<PipelineState>(0x1000);
    command.vertexBuffer = fakePointer<Buffer>(0x2000);
    command.indexBuffer = fakePointer<Buffer>(0x3000);
    command.indexCount = 36;
    command.indexType = IndexType::UInt32;
    command.firstIndex = 4;
    command.baseVertex = 2;
    command.topology = PrimitiveTopology::TriangleList;
    command.materialIndex = 7;
    command.albedoTex = fakePointer<Texture>(0x4000);
    command.normalTex = fakePointer<Texture>(0x5000);
    command.mrTex = fakePointer<Texture>(0x6000);
    command.emissiveTex = fakePointer<Texture>(0x7000);
    command.aoTex = fakePointer<Texture>(0x8000);
    command.sampler = fakePointer<Sampler>(0x9000);
    command.batchInstancingEligible = true;
    return command;
}

std::vector<GeometryDrawBatchRange> plan(size_t count) {
    std::vector<MeshDrawCommand> commands(count, batchCommand());
    std::vector<GeometryDrawBatchRange> result;
    planGeometryDrawBatches(commands, commands.empty() ? nullptr : commands.front().pipelineState, true, result);
    return result;
}

TEST(GeometryDrawBatchTests, CpuShaderObjectBatchAbiIsFixedAndPortable) {
    EXPECT_EQ(sizeof(ObjectUniforms), 128u);
    EXPECT_EQ(offsetof(ObjectUniforms, normalMat), 64u);
    EXPECT_EQ(offsetof(ObjectUniforms, pickId), 112u);
    EXPECT_EQ(kObjectBatchCapacity, 128u);
    EXPECT_EQ(sizeof(ObjectBatchUniforms), 16u * 1024u);
}

TEST(GeometryDrawBatchTests, PerObjectStateDoesNotSplitAnOtherwiseCompatibleBatch) {
    MeshDrawCommand first = batchCommand();
    MeshDrawCommand second = first;
    second.worldTransform = math::Mat4::translate(math::Vec3(3.0, 4.0, 5.0));
    second.pickId = 99;
    second.selected = true;
    second.hovered = true;

    EXPECT_TRUE(geometryDrawBatchCompatible(first, second));
}

TEST(GeometryDrawBatchTests, PackedObjectUniformsPreserveCommandOrderAndClearUnusedSlots) {
    std::vector<MeshDrawCommand> commands(3, batchCommand());
    for (size_t index = 0; index < commands.size(); ++index) {
        commands[index].worldTransform = math::Mat4::translate(math::Vec3(static_cast<double>(index + 1), 0.0, 0.0));
        commands[index].pickId = static_cast<uint32_t>(40 + index);
        commands[index].selected = index == 1;
        commands[index].hovered = index == 2;
    }
    ObjectBatchUniforms packed;
    ASSERT_TRUE(packGeometryDrawObjectBatch(commands, packed));

    for (size_t index = 0; index < commands.size(); ++index) {
        EXPECT_FLOAT_EQ(packed.objects[index].world[12], static_cast<float>(index + 1));
        EXPECT_EQ(packed.objects[index].pickId, 40u + index);
        EXPECT_EQ(packed.objects[index].selected, index == 1 ? 1u : 0u);
        EXPECT_EQ(packed.objects[index].hovered, index == 2 ? 1u : 0u);
    }
    EXPECT_EQ(packed.objects[3].pickId, 0u);
    EXPECT_FLOAT_EQ(packed.objects[3].world[0], 0.0f);
}

TEST(GeometryDrawBatchTests, EverySharedDrawStateFieldParticipatesInCompatibility) {
    const MeshDrawCommand baseline = batchCommand();
    const auto incompatible = [&](auto mutate) {
        MeshDrawCommand changed = baseline;
        mutate(changed);
        EXPECT_FALSE(geometryDrawBatchCompatible(baseline, changed));
    };

    incompatible([](auto& c) { c.pipelineState = fakePointer<PipelineState>(0x1010); });
    incompatible([](auto& c) { c.vertexBuffer = fakePointer<Buffer>(0x2010); });
    incompatible([](auto& c) { c.indexBuffer = fakePointer<Buffer>(0x3010); });
    incompatible([](auto& c) { ++c.indexCount; });
    incompatible([](auto& c) { c.indexType = IndexType::UInt16; });
    incompatible([](auto& c) { ++c.firstIndex; });
    incompatible([](auto& c) { ++c.baseVertex; });
    incompatible([](auto& c) { ++c.vertexCount; });
    incompatible([](auto& c) { c.topology = PrimitiveTopology::LineList; });
    incompatible([](auto& c) { ++c.materialIndex; });
    incompatible([](auto& c) { c.albedoTex = fakePointer<Texture>(0x4010); });
    incompatible([](auto& c) { c.normalTex = fakePointer<Texture>(0x5010); });
    incompatible([](auto& c) { c.mrTex = fakePointer<Texture>(0x6010); });
    incompatible([](auto& c) { c.emissiveTex = fakePointer<Texture>(0x7010); });
    incompatible([](auto& c) { c.aoTex = fakePointer<Texture>(0x8010); });
    incompatible([](auto& c) { c.sampler = fakePointer<Sampler>(0x9010); });
    incompatible([](auto& c) { c.isWire = !c.isWire; });
    incompatible([](auto& c) { c.batchInstancingEligible = false; });
}

TEST(GeometryDrawBatchTests, CandidateGateRejectsEverySemanticRisk) {
    const PipelineState* pipeline = fakePointer<PipelineState>(0x1000);
    const MeshDrawCommand baseline = batchCommand();
    EXPECT_TRUE(isGeometryDrawBatchCandidate(baseline, pipeline));

    const auto rejected = [&](auto mutate) {
        MeshDrawCommand changed = baseline;
        mutate(changed);
        EXPECT_FALSE(isGeometryDrawBatchCandidate(changed, pipeline));
    };
    rejected([](auto& c) { c.batchInstancingEligible = false; });
    rejected([](auto& c) { c.visible = false; });
    rejected([](auto& c) { c.translucent = true; });
    rejected([](auto& c) { c.instanceCount = 0; });
    rejected([](auto& c) { c.instanceCount = 2; });
    rejected([](auto& c) { c.pipelineState = fakePointer<PipelineState>(0x1010); });
    rejected([](auto& c) { c.vertexBuffer = nullptr; });
    rejected([](auto& c) {
        c.indexBuffer = nullptr;
        c.indexCount = 0;
        c.vertexCount = 0;
    });
}

TEST(GeometryDrawBatchTests, BoundarySplitsAvoidSmallMultiDrawTails) {
    EXPECT_EQ(plan(7), (std::vector{ GeometryDrawBatchRange{ 0, 7, false } }));
    EXPECT_EQ(plan(8), (std::vector{ GeometryDrawBatchRange{ 0, 8, true } }));
    EXPECT_EQ(plan(127), (std::vector{ GeometryDrawBatchRange{ 0, 127, true } }));
    EXPECT_EQ(plan(128), (std::vector{ GeometryDrawBatchRange{ 0, 128, true } }));
    EXPECT_EQ(plan(129),
              (std::vector{ GeometryDrawBatchRange{ 0, 128, true }, GeometryDrawBatchRange{ 128, 1, false } }));
    EXPECT_EQ(plan(130),
              (std::vector{ GeometryDrawBatchRange{ 0, 122, true }, GeometryDrawBatchRange{ 122, 8, true } }));
    EXPECT_EQ(plan(135),
              (std::vector{ GeometryDrawBatchRange{ 0, 127, true }, GeometryDrawBatchRange{ 127, 8, true } }));
    EXPECT_EQ(plan(256),
              (std::vector{ GeometryDrawBatchRange{ 0, 128, true }, GeometryDrawBatchRange{ 128, 128, true } }));
    EXPECT_EQ(plan(257), (std::vector{ GeometryDrawBatchRange{ 0, 128, true }, GeometryDrawBatchRange{ 128, 128, true },
                                       GeometryDrawBatchRange{ 256, 1, false } }));
}

TEST(GeometryDrawBatchTests, StateBreaksPreserveInputCoverageAndOrder) {
    std::vector<MeshDrawCommand> commands(18, batchCommand());
    commands[8].materialIndex = 8;
    commands[9].materialIndex = 8;
    commands[10].batchInstancingEligible = false;
    std::vector<GeometryDrawBatchRange> result;
    planGeometryDrawBatches(commands, commands.front().pipelineState, true, result);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], (GeometryDrawBatchRange{ 0, 8, true }));
    EXPECT_EQ(result[1], (GeometryDrawBatchRange{ 8, 10, false }));
}

}  // namespace
}  // namespace mulan::engine
