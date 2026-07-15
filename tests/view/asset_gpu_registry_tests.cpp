/**
 * @file asset_gpu_registry_tests.cpp
 * @brief 验证贴图内容版本去重与 submission token 延迟退役语义。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/render/asset_gpu_registry.h>
#include <mulan/rhi/device.h>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace mulan::engine {
namespace {

class RegistryTestFence final : public Fence {
public:
    Result<void> signal(uint64_t value) override {
        completed_ = value;
        return {};
    }
    Result<void> wait(uint64_t value) override {
        completed_ = value;
        return {};
    }
    uint64_t completedValue() const override { return completed_; }

private:
    uint64_t completed_ = 0;
};

class RegistryTestTexture final : public Texture {
public:
    RegistryTestTexture(TextureDesc desc, size_t& liveCount) : desc_(std::move(desc)), live_count_(liveCount) {
        ++live_count_;
    }
    ~RegistryTestTexture() override { --live_count_; }

    const TextureDesc& desc() const override { return desc_; }

private:
    TextureDesc desc_;
    size_t& live_count_;
};

class RegistryTestDevice final : public RHIDevice {
public:
    RegistryTestDevice() {
        auto fence = std::make_unique<RegistryTestFence>();
        fence_ = fence.get();
        initializeSubmissionTracking(std::move(fence));
    }

    ~RegistryTestDevice() override {
        drainDeferredReleases();
        shutdownSubmissionTracking();
    }

    SubmissionToken issueSubmission() {
        const SubmissionToken token = reserveSubmissionToken();
        commitSubmission(token);
        return token;
    }
    void complete(SubmissionToken token) { ASSERT_TRUE(fence_->signal(token.value)); }

    size_t createCount() const { return create_count_; }
    size_t uploadCount() const { return upload_count_; }
    size_t liveTextureCount() const { return live_texture_count_; }

    GraphicsBackend backend() const override { return GraphicsBackend::Vulkan; }
    const GPUDeviceCapabilities& capabilities() const override { return capabilities_; }
    const RenderConfig& renderConfig() const override { return render_config_; }
    math::Mat4 clipSpaceCorrectionMatrix() const override { return math::Mat4(1.0); }

    Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc&) override { return std::unique_ptr<Buffer>{}; }
    Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override {
        ++create_count_;
        std::unique_ptr<Texture> texture = std::make_unique<RegistryTestTexture>(desc, live_texture_count_);
        return std::move(texture);
    }
    Result<std::unique_ptr<Shader>> createShader(const ShaderDesc&) override { return std::unique_ptr<Shader>{}; }
    Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc&) override {
        return std::unique_ptr<PipelineState>{};
    }
    Result<std::unique_ptr<ComputePipelineState>> createComputePipelineState(const ComputePipelineDesc&) override {
        return std::unique_ptr<ComputePipelineState>{};
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override { return std::unique_ptr<CommandList>{}; }
    Result<std::unique_ptr<SwapChain>> createSwapChain(const SwapChainDesc&) override {
        return std::unique_ptr<SwapChain>{};
    }
    Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDesc&) override {
        return std::unique_ptr<RenderTarget>{};
    }
    Result<std::unique_ptr<Sampler>> createSampler(const SamplerDesc&) override { return std::unique_ptr<Sampler>{}; }
    Result<std::unique_ptr<Fence>> createFence(uint64_t) override { return std::unique_ptr<Fence>{}; }
    Result<std::unique_ptr<BindGroup>> createBindGroup(const BindGroupLayout&, const BindGroupDesc&) override {
        return std::unique_ptr<BindGroup>{};
    }

    Result<void> uploadTextureData(Texture*, const TextureUploadDesc&) override {
        ++upload_count_;
        return {};
    }
    Result<void> beginUploadBatch() override { return {}; }
    Result<void> flushUploadBatch() override { return {}; }
    Result<SubmissionToken> executeCommandLists(CommandList**, uint32_t, Fence*, uint64_t) override {
        return issueSubmission();
    }
    Result<void> waitIdle() override { return {}; }
    Result<CommandList*> beginFrame(SwapChain*) override { return static_cast<CommandList*>(nullptr); }
    Result<SubmissionToken> endFrame(SwapChain*) override { return issueSubmission(); }

private:
    RegistryTestFence* fence_ = nullptr;
    GPUDeviceCapabilities capabilities_;
    RenderConfig render_config_;
    size_t create_count_ = 0;
    size_t upload_count_ = 0;
    size_t live_texture_count_ = 0;
};

TEST(AssetGpuRegistryTests, TextureRevisionReusesSameVersionAndDefersReplacedTextureDestruction) {
    RegistryTestDevice device;
    {
        AssetGpuRegistry registry(device);
        const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
        ASSERT_TRUE(image);
        ASSERT_TRUE(image->valid());

        const AssetGpuKey key = makeAssetGpuKey(7);
        auto first = registry.acquireTexture(key, *image, {}, 1);
        ASSERT_TRUE(first);
        ASSERT_NE(*first, nullptr);
        EXPECT_EQ(device.createCount(), 1u);
        EXPECT_EQ(device.uploadCount(), 1u);

        auto reused = registry.acquireTexture(key, *image, {}, 1);
        ASSERT_TRUE(reused);
        EXPECT_EQ(*reused, *first);
        EXPECT_EQ(device.createCount(), 1u);
        EXPECT_EQ(device.uploadCount(), 1u);

        const SubmissionToken inFlight = device.issueSubmission();
        auto replaced = registry.acquireTexture(key, *image, {}, 2);
        ASSERT_TRUE(replaced);
        EXPECT_NE(*replaced, *first);
        EXPECT_EQ(device.createCount(), 2u);
        EXPECT_EQ(device.uploadCount(), 2u);
        EXPECT_EQ(device.liveTextureCount(), 2u);

        device.collectGarbage();
        EXPECT_EQ(device.liveTextureCount(), 2u);
        device.complete(inFlight);
        device.collectGarbage();
        EXPECT_EQ(device.liveTextureCount(), 1u);
    }
    EXPECT_EQ(device.liveTextureCount(), 0u);
}

TEST(AssetGpuRegistryTests, TextureRetireUsesCompleteOptionsIdentityAndDefersDestruction) {
    RegistryTestDevice device;
    {
        AssetGpuRegistry registry(device);
        const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
        ASSERT_TRUE(image);

        const AssetGpuKey key = makeAssetGpuKey(8);
        TextureLoadOptions linearOptions;
        linearOptions.sRGB = false;
        linearOptions.generateMips = true;
        TextureLoadOptions srgbOptions = linearOptions;
        srgbOptions.sRGB = true;

        ASSERT_TRUE(registry.acquireTexture(key, *image, linearOptions, 1));
        ASSERT_TRUE(registry.acquireTexture(key, *image, srgbOptions, 1));
        ASSERT_EQ(registry.textureCount(), 2u);
        ASSERT_EQ(device.liveTextureCount(), 2u);

        const SubmissionToken inFlight = device.issueSubmission();
        auto retired = registry.retireTexture(key, srgbOptions);
        ASSERT_TRUE(retired);
        EXPECT_TRUE(*retired);
        EXPECT_EQ(registry.textureCount(), 1u);
        EXPECT_EQ(registry.findTexture(key, srgbOptions), nullptr);
        EXPECT_NE(registry.findTexture(key, linearOptions), nullptr);

        device.collectGarbage();
        EXPECT_EQ(device.liveTextureCount(), 2u);
        device.complete(inFlight);
        device.collectGarbage();
        EXPECT_EQ(device.liveTextureCount(), 1u);
    }
    EXPECT_EQ(device.liveTextureCount(), 0u);
}

TEST(AssetGpuRegistryTests, ReplacedThenRetiredTextureOwnsEachInstanceExactlyOnce) {
    RegistryTestDevice device;
    {
        AssetGpuRegistry registry(device);
        const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
        ASSERT_TRUE(image);

        const AssetGpuKey key = makeAssetGpuKey(9);
        ASSERT_TRUE(registry.acquireTexture(key, *image, {}, 1));
        const SubmissionToken inFlight = device.issueSubmission();
        ASSERT_TRUE(registry.acquireTexture(key, *image, {}, 2));
        ASSERT_EQ(registry.textureCount(), 1u);
        ASSERT_EQ(device.liveTextureCount(), 2u);

        auto retired = registry.retireTexture(key);
        ASSERT_TRUE(retired);
        EXPECT_TRUE(*retired);
        EXPECT_EQ(registry.textureCount(), 0u);
        EXPECT_EQ(registry.findTexture(key), nullptr);
        EXPECT_EQ(device.liveTextureCount(), 2u);

        // 映射已移除后重复 retire 是幂等空操作，不能再次拥有或销毁旧实例。
        auto repeated = registry.retireTexture(key);
        ASSERT_TRUE(repeated);
        EXPECT_FALSE(*repeated);

        device.collectGarbage();
        EXPECT_EQ(device.liveTextureCount(), 2u);
        device.complete(inFlight);
        device.collectGarbage();
        EXPECT_EQ(device.liveTextureCount(), 0u);
    }
    EXPECT_EQ(device.liveTextureCount(), 0u);
}

}  // namespace
}  // namespace mulan::engine
