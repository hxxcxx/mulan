#include <gtest/gtest.h>

#include <mulan/rhi/bind_group.h>
#include <mulan/rhi/buffer.h>

#include <array>

namespace mulan::engine {
namespace {

class TestBuffer final : public Buffer {
public:
    explicit TestBuffer(BufferDesc desc) : desc_(desc) {}

    const BufferDesc& desc() const override { return desc_; }
    core::Result<void> write(uint32_t, uint32_t, const void*) override { return {}; }
    core::Result<void> readback(uint32_t, uint32_t, void*) override { return {}; }

private:
    BufferDesc desc_;
};

BindGroupLayout uniformLayout() {
    const std::array entries{ BindGroupLayoutEntry{ 2, 1, DescriptorType::UniformBuffer, PipelineBinding::kStageVertex,
                                                    BindingMode::Static } };
    return BindGroupLayout::fromBindings(entries);
}

BindGroupLayout dynamicUniformLayout() {
    const std::array entries{ BindGroupLayoutEntry{ 2, 1, DescriptorType::UniformBuffer, PipelineBinding::kStageVertex,
                                                    BindingMode::Dynamic } };
    return BindGroupLayout::fromBindings(entries);
}

TEST(BindGroupValidationTest, AcceptsExplicitAlignedUniformRange) {
    TestBuffer buffer(BufferDesc::uniform(1024, "Uniform"));
    BindGroupDesc desc;
    desc.addUniformBuffer(2, &buffer, 256, 128);

    EXPECT_TRUE(validateBindGroupDesc(uniformLayout(), desc, { 256, 64 * 1024 }).empty());
    EXPECT_EQ(desc.entries[0].type, DescriptorType::UniformBuffer);
}

TEST(BindGroupValidationTest, RejectsEntryTypeThatDiffersFromLayout) {
    BindGroupDesc desc;
    desc.addTexture(2, nullptr);

    EXPECT_FALSE(validateBindGroupDesc(uniformLayout(), desc, { 256, 64 * 1024 }).empty());
}

TEST(BindGroupValidationTest, RejectsBufferWithoutUniformUsage) {
    TestBuffer buffer(BufferDesc::dynamicVertex(1024, "Vertex"));
    BindGroupDesc desc;
    desc.addUniformBuffer(2, &buffer, 0, 128);

    EXPECT_FALSE(validateBindGroupDesc(uniformLayout(), desc, { 256, 64 * 1024 }).empty());
}

TEST(BindGroupValidationTest, RejectsMisalignedOrOutOfBoundsUniformRange) {
    TestBuffer buffer(BufferDesc::uniform(1024, "Uniform"));
    BindGroupDesc misaligned;
    misaligned.addUniformBuffer(2, &buffer, 16, 128);
    BindGroupDesc outOfBounds;
    outOfBounds.addUniformBuffer(2, &buffer, 768, 512);

    EXPECT_FALSE(validateBindGroupDesc(uniformLayout(), misaligned, { 256, 64 * 1024 }).empty());
    EXPECT_FALSE(validateBindGroupDesc(uniformLayout(), outOfBounds, { 256, 64 * 1024 }).empty());
}

TEST(BindGroupValidationTest, RejectsUniformRangeLargerThanDeviceLimit) {
    TestBuffer buffer(BufferDesc::uniform(1024, "Uniform"));
    BindGroupDesc desc;
    desc.addUniformBuffer(2, &buffer, 0, 768);

    EXPECT_FALSE(validateBindGroupDesc(uniformLayout(), desc, { 256, 512 }).empty());
}

TEST(BindGroupValidationTest, AllowsDynamicUniformToBeOmittedFromBindGroupDesc) {
    const BindGroupDesc desc;

    EXPECT_TRUE(validateBindGroupDesc(dynamicUniformLayout(), desc, { 256, 64 * 1024 }).empty());
}

TEST(BindGroupValidationTest, RejectsDynamicBindingsWithoutUniformSemantics) {
    const std::array entries{ BindGroupLayoutEntry{ 2, 1, DescriptorType::TextureSRV, PipelineBinding::kStageFragment,
                                                    BindingMode::Dynamic } };
    const BindGroupLayout layout = BindGroupLayout::fromBindings(entries);
    const BindGroupDesc desc;

    EXPECT_FALSE(validateBindGroupDesc(layout, desc, { 256, 64 * 1024 }).empty());
}

TEST(BindGroupValidationTest, IncludesBindingModeInLayoutIdentity) {
    EXPECT_NE(uniformLayout().hash(), dynamicUniformLayout().hash());
    EXPECT_NE(uniformLayout(), dynamicUniformLayout());
}

TEST(BindGroupValidationTest, ComparesCompleteLayoutEntriesAfterHashPrefilter) {
    const std::array firstEntries{
        BindGroupLayoutEntry{ 4, 1, DescriptorType::Sampler, PipelineBinding::kStageFragment, BindingMode::Static },
        BindGroupLayoutEntry{ 1, 1, DescriptorType::TextureSRV, PipelineBinding::kStageFragment, BindingMode::Static },
    };
    const std::array reorderedEntries{
        BindGroupLayoutEntry{ 1, 1, DescriptorType::TextureSRV, PipelineBinding::kStageFragment, BindingMode::Static },
        BindGroupLayoutEntry{ 4, 1, DescriptorType::Sampler, PipelineBinding::kStageFragment, BindingMode::Static },
    };
    const BindGroupLayout first = BindGroupLayout::fromBindings(firstEntries);
    const BindGroupLayout reordered = BindGroupLayout::fromBindings(reorderedEntries);

    EXPECT_EQ(first, reordered);
    EXPECT_EQ(first.entries(), reordered.entries());

    BindGroupLayout simulatedCollision = first;
    // 测试中只改规范化条目并保留原 hash，用于稳定复现真实 hash collision 的比较路径。
    auto& collisionEntries = const_cast<std::vector<BindGroupLayoutEntry>&>(simulatedCollision.entries());
    collisionEntries.front().stages = PipelineBinding::kStageVertex;
    ASSERT_EQ(first.hash(), simulatedCollision.hash());
    EXPECT_NE(first, simulatedCollision);
}

TEST(BindGroupValidationTest, ReportsBuilderOverflowInsteadOfSilentlyTruncating) {
    BindGroupDesc desc;
    for (uint32_t binding = 0; binding <= BindGroupDesc::kMaxEntries; ++binding)
        desc.addTexture(binding, nullptr);

    EXPECT_TRUE(desc.overflowed());
    EXPECT_EQ(desc.count, BindGroupDesc::kMaxEntries);
    EXPECT_FALSE(validateBindGroupDesc(BindGroupLayout::empty(), desc, { 256, 64 * 1024 }).empty());

    desc.clear();
    EXPECT_FALSE(desc.overflowed());
    EXPECT_EQ(desc.count, 0);
}

TEST(BindGroupValidationTest, RejectsDescriptorArrayLayoutsAtTheBindGroupBoundary) {
    const std::array bindings{
        BindGroupLayoutEntry{ 0, 2, DescriptorType::TextureSRV, PipelineBinding::kStageFragment, BindingMode::Static },
    };
    const BindGroupLayout layout = BindGroupLayout::fromBindings(bindings);
    BindGroupDesc desc;
    desc.addTexture(0, nullptr);

    EXPECT_FALSE(validateBindGroupDesc(layout, desc, { 256, 64 * 1024 }).empty());
}

TEST(BindGroupValidationTest, DerivesComputeLayoutsFromTheirDeclaredBindings) {
    ComputePipelineDesc desc;
    desc.descriptorBindingCount = 1;
    desc.descriptorBindings[0] = { 4, 1, DescriptorType::UniformBuffer, PipelineBinding::kStageCompute,
                                   BindingMode::Dynamic };

    const BindGroupLayout layout = BindGroupLayout::fromPipelineDesc(desc);
    ASSERT_EQ(layout.entries().size(), 1u);
    EXPECT_EQ(layout.entries()[0].binding, 4u);
    EXPECT_EQ(layout.entries()[0].mode, BindingMode::Dynamic);
}

TEST(BindGroupValidationTest, AcceptsDynamicUniformFromCurrentRecording) {
    TestBuffer buffer(BufferDesc::uniform(1024, "TransientUniformPage"));
    const std::array bindings{ DynamicUniformBinding{ 2, { &buffer, 256, 128, 7 } } };

    EXPECT_TRUE(validateDynamicUniformBindings(dynamicUniformLayout(), bindings, { 256, 64 * 1024 }, 7).empty());
}

TEST(BindGroupValidationTest, RejectsDynamicUniformFromAnotherRecording) {
    TestBuffer buffer(BufferDesc::uniform(1024, "TransientUniformPage"));
    const std::array bindings{ DynamicUniformBinding{ 2, { &buffer, 256, 128, 6 } } };

    EXPECT_FALSE(validateDynamicUniformBindings(dynamicUniformLayout(), bindings, { 256, 64 * 1024 }, 7).empty());
}

}  // namespace
}  // namespace mulan::engine
