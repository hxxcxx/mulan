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
    void update(uint32_t, uint32_t, const void*) override {}

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
