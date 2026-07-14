/**
 * @file device_lifetime_tests.cpp
 * @brief RHI Device 资源来源与退休批次测试
 * @author hxxcxx
 * @date 2026-07-14
 */

#include <mulan/rhi/device.h>
#include <mulan/rhi/engine_error_code.h>
#include <mulan/rhi/pipeline_validation.h>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace mulan::engine {
namespace {

class TestFence final : public Fence {
public:
    core::Result<void> signal(uint64_t value) override {
        completed_ = value;
        return {};
    }
    core::Result<void> wait(uint64_t value) override {
        completed_ = value;
        return {};
    }
    uint64_t completedValue() const override { return completed_; }

private:
    uint64_t completed_ = 0;
};

class TestDevice final : public RHIDevice {
public:
    TestDevice() {
        auto fence = std::make_unique<TestFence>();
        fence_ = fence.get();
        initializeSubmissionTracking(std::move(fence));
    }

    ~TestDevice() override {
        drainDeferredReleases();
        shutdownSubmissionTracking();
    }

    SubmissionToken issueSubmission() {
        SubmissionToken token = reserveSubmissionToken();
        commitSubmission(token);
        return token;
    }

    void complete(SubmissionToken token) { ASSERT_TRUE(fence_->signal(token.value)); }
    GPUDeviceCapabilities& mutableCapabilities() { return capabilities_; }

    GraphicsBackend backend() const override { return GraphicsBackend::Vulkan; }
    const GPUDeviceCapabilities& capabilities() const override { return capabilities_; }
    const RenderConfig& renderConfig() const override { return render_config_; }
    math::Mat4 clipSpaceCorrectionMatrix() const override { return math::Mat4(1.0); }

    core::Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc&) override { return std::unique_ptr<Buffer>{}; }
    core::Result<std::unique_ptr<Texture>> createTexture(const TextureDesc&) override {
        return std::unique_ptr<Texture>{};
    }
    core::Result<std::unique_ptr<Shader>> createShader(const ShaderDesc&) override { return std::unique_ptr<Shader>{}; }
    core::Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc&) override {
        return std::unique_ptr<PipelineState>{};
    }
    core::Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(
            const ComputePipelineDesc&) override {
        return std::unique_ptr<ComputePipelineState>{};
    }
    core::Result<std::unique_ptr<CommandList>> createCommandList() override { return std::unique_ptr<CommandList>{}; }
    core::Result<std::unique_ptr<SwapChain>> createSwapChain(const SwapChainDesc&) override {
        return std::unique_ptr<SwapChain>{};
    }
    core::Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc&) override {
        return std::unique_ptr<RenderTarget>{};
    }
    core::Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc&) override {
        return std::unique_ptr<Sampler>{};
    }
    core::Result<std::unique_ptr<Fence>> createFence(uint64_t) override { return std::unique_ptr<Fence>{}; }
    core::Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout&, const BindGroupDesc&) override {
        return std::unique_ptr<BindGroup>{};
    }

    core::Result<void> uploadTextureData(Texture*, const TextureUploadDesc&) override { return {}; }
    core::Result<void> beginUploadBatch() override { return {}; }
    core::Result<void> flushUploadBatch() override { return {}; }
    core::Result<SubmissionToken> executeCommandLists(CommandList**, uint32_t, Fence*, uint64_t) override {
        return issueSubmission();
    }
    core::Result<void> waitIdle() override { return {}; }
    core::Result<CommandList*> beginFrame(SwapChain*) override { return static_cast<CommandList*>(nullptr); }
    core::Result<SubmissionToken> endFrame(SwapChain*) override { return issueSubmission(); }

private:
    TestFence* fence_ = nullptr;
    GPUDeviceCapabilities capabilities_;
    RenderConfig render_config_;
};

class TestResource final : public RHITrackedResource {
public:
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::Buffer, "TestResource"); }
};

class TestShader final : public Shader {
public:
    explicit TestShader(ShaderType type) { desc_.type = type; }

    const ShaderDesc& desc() const override { return desc_; }
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::Shader, "TestShader"); }

private:
    ShaderDesc desc_;
};

TEST(DeviceLifetimeTest, IdentifiesTheOwningDevice) {
    TestDevice first;
    TestDevice second;
    TestResource resource;

    resource.attach(first);

    EXPECT_TRUE(resource.belongsTo(first));
    EXPECT_FALSE(resource.belongsTo(second));
}

TEST(PipelineValidationTest, AcceptsAConsistentGraphicsContract) {
    TestDevice device;
    TestShader vertexShader(ShaderType::Vertex);
    vertexShader.attach(device);

    GraphicsPipelineDesc desc;
    desc.vs = &vertexShader;
    desc.depthStencil.depthEnable = false;
    desc.colorTargetCount = 1;
    desc.colorFormats[0] = TextureFormat::RGBA8_UNorm;

    EXPECT_TRUE(validateGraphicsPipelineDesc(desc, device, device.capabilities()));
}

TEST(PipelineValidationTest, RejectsDuplicateOrOutOfRangeBindings) {
    TestDevice device;
    TestShader vertexShader(ShaderType::Vertex);
    vertexShader.attach(device);

    GraphicsPipelineDesc desc;
    desc.vs = &vertexShader;
    desc.depthStencil.depthEnable = false;
    desc.descriptorBindingCount = 2;
    desc.descriptorBindings[0] = { 3, 1, DescriptorType::UniformBuffer, PipelineBinding::kStageVertex };
    desc.descriptorBindings[1] = { 3, 1, DescriptorType::Sampler, PipelineBinding::kStageFragment };
    EXPECT_FALSE(validateGraphicsPipelineDesc(desc, device, device.capabilities()));

    desc.descriptorBindingCount = GraphicsPipelineDesc::kMaxDescriptorBindings + 1;
    EXPECT_FALSE(validateGraphicsPipelineDesc(desc, device, device.capabilities()));
}

TEST(PipelineValidationTest, RejectsUnsupportedDynamicAndPushConstantBindings) {
    TestDevice device;
    TestShader vertexShader(ShaderType::Vertex);
    vertexShader.attach(device);

    GraphicsPipelineDesc desc;
    desc.vs = &vertexShader;
    desc.depthStencil.depthEnable = false;
    desc.descriptorBindingCount = 1;
    desc.descriptorBindings[0] = { 0, 1, DescriptorType::TextureSRV, PipelineBinding::kStageFragment,
                                   BindingMode::Dynamic };
    EXPECT_FALSE(validateGraphicsPipelineDesc(desc, device, device.capabilities()));

    desc.descriptorBindingCount = 0;
    desc.pushConstantSize = 16;
    EXPECT_FALSE(validateGraphicsPipelineDesc(desc, device, device.capabilities()));
}

TEST(PipelineValidationTest, RejectsDescriptorArraysUntilBindGroupCanRepresentThem) {
    TestDevice device;
    TestShader vertexShader(ShaderType::Vertex);
    vertexShader.attach(device);

    GraphicsPipelineDesc desc;
    desc.vs = &vertexShader;
    desc.depthStencil.depthEnable = false;
    desc.descriptorBindingCount = 1;
    desc.descriptorBindings[0] = { 0, 2, DescriptorType::TextureSRV, PipelineBinding::kStageFragment };

    const auto result = validateGraphicsPipelineDesc(desc, device, device.capabilities());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, static_cast<int32_t>(EngineErrorCode::PipelineCreateFailed));
}

TEST(ResourceDescriptionTest, DiscardsCreationOnlyPointersButKeepsOwnedNames) {
    std::array<uint8_t, 4> bytes{ 1, 2, 3, 4 };
    BufferDesc buffer = BufferDesc::vertex(static_cast<uint32_t>(bytes.size()), bytes.data(), std::string("Buffer"));
    buffer.discardInitialData();
    EXPECT_EQ(buffer.name, "Buffer");
    EXPECT_EQ(buffer.initData, nullptr);

    ShaderDesc shader;
    shader.name = std::string("Shader");
    shader.source = "void main() {}";
    shader.byteCode = bytes.data();
    shader.byteCodeSize = static_cast<uint32_t>(bytes.size());
    shader.discardCreationData();
    EXPECT_EQ(shader.name, "Shader");
    EXPECT_TRUE(shader.source.empty());
    EXPECT_EQ(shader.byteCode, nullptr);
}

TEST(DeviceLifetimeTest, ReleasesCompletedSubmissionBatchesInOrder) {
    TestDevice device;
    const SubmissionToken first = device.issueSubmission();
    const SubmissionToken second = device.issueSubmission();
    std::vector<int> released;

    ASSERT_TRUE(device.retire(first, [&released] { released.push_back(1); }));
    ASSERT_TRUE(device.retire(first, [&released] { released.push_back(2); }));
    ASSERT_TRUE(device.retire(second, [&released] { released.push_back(3); }));

    device.complete(first);
    device.collectGarbage();
    EXPECT_EQ(released, (std::vector<int>{ 1, 2 }));

    device.complete(second);
    device.collectGarbage();
    EXPECT_EQ(released, (std::vector<int>{ 1, 2, 3 }));
}

TEST(DeviceLifetimeTest, ReleasesAlreadyCompletedSubmissionImmediately) {
    TestDevice device;
    const SubmissionToken token = device.issueSubmission();
    bool released = false;
    device.complete(token);

    ASSERT_TRUE(device.retire(token, [&released] { released = true; }));

    EXPECT_TRUE(released);
}

}  // namespace
}  // namespace mulan::engine
