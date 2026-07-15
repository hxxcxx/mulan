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
#include <span>
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
    core::Result<void> validateBatch(CommandList** lists, uint32_t count) const {
        return validateCommandListsForSubmission(lists, count);
    }

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
    core::Result<SubmissionToken> executeCommandLists(CommandList** lists, uint32_t count, Fence*, uint64_t) override {
        if (auto validation = validateCommandListsForSubmission(lists, count); !validation)
            return std::unexpected(validation.error());
        const SubmissionToken token = issueSubmission();
        lists[0]->markSubmitted(token);
        return token;
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

class TestBuffer final : public Buffer {
public:
    TestBuffer() { desc_ = BufferDesc::dynamicVertex(64, "TestBuffer"); }
    ~TestBuffer() override { waitForLastUseBeforeDestruction(); }
    const BufferDesc& desc() const override { return desc_; }
    core::Result<void> write(uint32_t, uint32_t, const void*) override { return {}; }
    core::Result<void> readback(uint32_t, uint32_t, void*) override { return {}; }
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::Buffer, desc_.name); }

private:
    BufferDesc desc_;
};

class TestTexture final : public Texture {
public:
    TestTexture(TextureFormat format = TextureFormat::RGBA8_UNorm, uint32_t sampleCount = 1)
        : desc_(TextureDesc::renderTarget(1, 1, format, "TestTexture", sampleCount)) {}
    const TextureDesc& desc() const override { return desc_; }
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::Texture, "TestTexture"); }

private:
    TextureDesc desc_;
};

class TestPipeline final : public PipelineState {
public:
    const GraphicsPipelineDesc& desc() const override { return desc_; }
    GraphicsPipelineDesc& mutableDesc() { return desc_; }
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::PipelineState, "TestPipeline"); }

private:
    GraphicsPipelineDesc desc_;
};

class TestBindGroup final : public BindGroup {
public:
    TestBindGroup() : layout_(BindGroupLayout::fromBindings(std::span<const BindGroupLayoutEntry>{})) {}
    const BindGroupLayout& layout() const override { return layout_; }
    const BindGroupEntry* entries() const override { return nullptr; }
    uint8_t entryCount() const override { return 0; }
    bool updateUBO(uint32_t, Buffer*, uint32_t, uint32_t) override { return false; }
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::BindGroup, "TestBindGroup"); }

private:
    BindGroupLayout layout_;
};

class TestCommandList final : public CommandList {
public:
    void attach(RHIDevice& device) { trackResource(device, RHIResourceKind::CommandList, "TestCommandList"); }
    uint32_t drawCount() const { return draw_count_; }
    DescriptorCacheEpoch epoch(uint64_t generation) const { return descriptorCacheEpoch(generation); }

    core::Result<void> doBegin() override { return {}; }
    core::Result<void> doEnd() override { return {}; }
    void doSetPipelineState(PipelineState*) override {}
    void doSetComputePipelineState(ComputePipelineState*) override {}
    void doBindGroup(BindGroup&) override {}
    void doSetViewport(const Viewport&) override {}
    void doSetScissorRect(const ScissorRect&) override {}
    void doSetVertexBuffer(uint32_t, Buffer*, uint32_t) override {}
    void doSetVertexBuffers(uint32_t, uint32_t, Buffer**, uint32_t*) override {}
    void doSetIndexBuffer(Buffer*, uint32_t, IndexType) override {}
    void doDraw(const DrawAttribs&) override { ++draw_count_; }
    void doDrawIndexed(const DrawIndexedAttribs&) override { ++draw_count_; }
    void doDrawIndirect(Buffer*, uint32_t, uint32_t, uint32_t) override { ++draw_count_; }
    void doDispatch(uint32_t, uint32_t, uint32_t) override {}
    void doDispatchIndirect(Buffer*, uint32_t) override {}
    void doSetPushConstants(uint32_t, uint32_t, const void*, uint32_t) override {}
    void doTransitionResource(Texture*, ResourceState) override {}
    core::Result<void> doCopyTextureToBuffer(Texture*, Buffer*) override { return {}; }
    core::Result<void> doBeginRenderPass(const RenderPassBeginInfo&) override { return {}; }
    void doEndRenderPass() override {}

private:
    uint32_t draw_count_ = 0;
};

TEST(DeviceLifetimeTest, IdentifiesTheOwningDevice) {
    TestDevice first;
    TestDevice second;
    TestResource resource;

    resource.attach(first);

    EXPECT_TRUE(resource.belongsTo(first));
    EXPECT_FALSE(resource.belongsTo(second));
}

TEST(CommandListContractTest, RejectsCommandsOutsideRecordingAndRecoversOnBegin) {
    TestDevice device;
    TestCommandList command;
    command.attach(device);

    command.setViewport({});
    ASSERT_EQ(command.state(), CommandList::State::Invalid);
    ASSERT_NE(command.recordingError(), nullptr);

    EXPECT_TRUE(command.begin());
    EXPECT_TRUE(command.end());
    EXPECT_EQ(command.state(), CommandList::State::Executable);
}

TEST(CommandListContractTest, DrawRequiresRenderPassAndGraphicsPipeline) {
    TestDevice device;
    TestCommandList command;
    command.attach(device);
    ASSERT_TRUE(command.begin());

    command.draw({ 3 });
    ASSERT_EQ(command.state(), CommandList::State::Invalid);
    ASSERT_NE(command.recordingError(), nullptr);
    const std::string firstError = command.recordingError()->message;
    command.setVertexBuffer(0, nullptr);
    EXPECT_EQ(command.recordingError()->message, firstError);
    EXPECT_FALSE(command.end());
}

TEST(CommandListContractTest, RecordsAValidGraphicsSequence) {
    TestDevice device;
    TestPipeline pipeline;
    TestBindGroup bindGroup;
    TestCommandList command;
    pipeline.attach(device);
    bindGroup.attach(device);
    command.attach(device);

    ASSERT_TRUE(command.begin());
    command.setPipelineState(&pipeline);
    command.bindGroup(bindGroup);
    RenderPassBeginInfo pass;
    pass.width = 1;
    pass.height = 1;
    command.beginRenderPass(pass);
    command.draw({ 3 });
    command.endRenderPass();
    ASSERT_TRUE(command.end());
    EXPECT_EQ(command.drawCount(), 1u);
}

TEST(CommandListContractTest, RejectsTransferInsideRenderPass) {
    TestDevice device;
    TestPipeline pipeline;
    TestTexture texture;
    TestCommandList command;
    pipeline.attach(device);
    texture.attach(device);
    command.attach(device);

    ASSERT_TRUE(command.begin());
    command.setPipelineState(&pipeline);
    RenderPassBeginInfo pass;
    pass.width = 1;
    pass.height = 1;
    command.beginRenderPass(pass);
    command.transitionResource(&texture, ResourceState::CopySrc);
    EXPECT_EQ(command.state(), CommandList::State::Invalid);
    EXPECT_FALSE(command.end());
}

TEST(CommandListContractTest, RejectsResourcesFromAnotherDevice) {
    TestDevice first;
    TestDevice second;
    TestBuffer buffer;
    TestCommandList command;
    buffer.attach(second);
    command.attach(first);

    ASSERT_TRUE(command.begin());
    command.setVertexBuffer(0, &buffer);
    EXPECT_EQ(command.state(), CommandList::State::Invalid);
    EXPECT_FALSE(command.end());
}

TEST(CommandListContractTest, DescriptorEpochIncludesCommandListIdentity) {
    TestCommandList first;
    TestCommandList second;

    EXPECT_NE(first.epoch(1), second.epoch(1));
    EXPECT_NE(first.epoch(1), first.epoch(2));
    EXPECT_EQ(first.epoch(7), first.epoch(7));
}

TEST(CommandListContractTest, RejectsMultiListSubmissionUntilStateMergingExists) {
    TestDevice device;
    TestCommandList first;
    TestCommandList second;
    first.attach(device);
    second.attach(device);
    ASSERT_TRUE(first.begin());
    ASSERT_TRUE(first.end());
    ASSERT_TRUE(second.begin());
    ASSERT_TRUE(second.end());
    CommandList* lists[] = { &first, &second };

    const auto result = device.validateBatch(lists, 2);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, static_cast<int32_t>(EngineErrorCode::SubmissionFailed));
}

TEST(CommandListContractTest, RejectsAResourceDestroyedAfterRecording) {
    TestDevice device;
    TestCommandList command;
    command.attach(device);
    auto buffer = std::make_unique<TestBuffer>();
    buffer->attach(device);
    ASSERT_TRUE(command.begin());
    command.setVertexBuffer(0, buffer.get());
    ASSERT_TRUE(command.end());
    buffer.reset();
    CommandList* list = &command;

    const auto result = device.validateBatch(&list, 1);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, static_cast<int32_t>(EngineErrorCode::SubmissionFailed));
}

TEST(CommandListContractTest, ResourceDestructionWaitsForItsLastSubmission) {
    TestDevice device;
    TestCommandList command;
    command.attach(device);
    auto buffer = std::make_unique<TestBuffer>();
    buffer->attach(device);
    ASSERT_TRUE(command.begin());
    command.setVertexBuffer(0, buffer.get());
    ASSERT_TRUE(command.end());
    auto submission = device.executeCommandList(&command);
    ASSERT_TRUE(submission);
    ASSERT_FALSE(device.isSubmissionComplete(*submission));

    buffer.reset();

    EXPECT_TRUE(device.isSubmissionComplete(*submission));
}

TEST(CommandListContractTest, RejectsPipelineAndRenderPassFormatMismatchInEitherOrder) {
    TestDevice device;
    TestTexture color(TextureFormat::RGBA8_UNorm);
    TestPipeline pipeline;
    TestCommandList pipelineFirst;
    TestCommandList passFirst;
    color.attach(device);
    pipeline.attach(device);
    pipelineFirst.attach(device);
    passFirst.attach(device);
    pipeline.mutableDesc().colorTargetCount = 1;
    pipeline.mutableDesc().colorFormats[0] = TextureFormat::BGRA8_UNorm;
    RenderPassBeginInfo pass;
    pass.width = 1;
    pass.height = 1;
    pass.colorCount = 1;
    pass.colorAttachments[0].target = &color;

    ASSERT_TRUE(pipelineFirst.begin());
    pipelineFirst.setPipelineState(&pipeline);
    pipelineFirst.beginRenderPass(pass);
    EXPECT_EQ(pipelineFirst.state(), CommandList::State::Invalid);

    ASSERT_TRUE(passFirst.begin());
    passFirst.beginRenderPass(pass);
    passFirst.setPipelineState(&pipeline);
    EXPECT_EQ(passFirst.state(), CommandList::State::Invalid);
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
