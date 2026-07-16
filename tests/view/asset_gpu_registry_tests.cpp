/**
 * @file asset_gpu_registry_tests.cpp
 * @brief 验证贴图内容版本去重与 submission token 延迟退役语义。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/render/asset_gpu_registry.h>
#include <mulan/render/device_resource_service.h>
#include <mulan/render/frame/render_target_info.h>
#include <mulan/render/gpu_scene_contract.h>
#include <mulan/render/light_environment.h>
#include <mulan/rhi/device.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace mulan::engine {
namespace {

class RegistryTestFence final : public Fence {
public:
    ResultVoid signal(uint64_t value) override {
        completed_ = value;
        return {};
    }
    ResultVoid wait(uint64_t value) override {
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

class RegistryTestBuffer final : public Buffer {
public:
    explicit RegistryTestBuffer(BufferDesc desc) : desc_(std::move(desc)) { desc_.discardInitialData(); }

    const BufferDesc& desc() const override { return desc_; }
    ResultVoid write(uint32_t, uint32_t, const void*) override { return {}; }
    ResultVoid readback(uint32_t, uint32_t, void*) override { return {}; }

private:
    BufferDesc desc_;
};

class RegistryTestShader final : public Shader {
public:
    explicit RegistryTestShader(const ShaderDesc& desc) : desc_(desc) { desc_.discardCreationData(); }
    const ShaderDesc& desc() const override { return desc_; }

private:
    ShaderDesc desc_;
};

class RegistryTestPipeline final : public PipelineState {
public:
    explicit RegistryTestPipeline(const GraphicsPipelineDesc& desc) : desc_(desc) { desc_.discardShaderReferences(); }
    const GraphicsPipelineDesc& desc() const override { return desc_; }

private:
    GraphicsPipelineDesc desc_;
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
    void failNextBufferCreate() { fail_next_buffer_create_ = true; }
    void failNextTextureCreate() { fail_next_texture_create_ = true; }

    size_t createCount() const { return create_count_; }
    size_t uploadCount() const { return upload_count_; }
    size_t liveTextureCount() const { return live_texture_count_; }
    size_t pipelineCreateCount() const { return pipeline_create_count_; }

    GraphicsBackend backend() const override { return GraphicsBackend::Vulkan; }
    const GPUDeviceCapabilities& capabilities() const override { return capabilities_; }
    const RenderConfig& renderConfig() const override { return render_config_; }
    math::Mat4 clipSpaceCorrectionMatrix() const override { return math::Mat4(1.0); }

    Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc) override {
        if (fail_next_buffer_create_) {
            fail_next_buffer_create_ = false;
            return std::unexpected(Error::make(ErrorCode::Internal, "Injected buffer creation failure."));
        }
        std::unique_ptr<Buffer> buffer = std::make_unique<RegistryTestBuffer>(desc);
        return std::move(buffer);
    }
    Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override {
        if (fail_next_texture_create_) {
            fail_next_texture_create_ = false;
            return std::unexpected(Error::make(ErrorCode::Internal, "Injected texture creation failure."));
        }
        ++create_count_;
        std::unique_ptr<Texture> texture = std::make_unique<RegistryTestTexture>(desc, live_texture_count_);
        return std::move(texture);
    }
    Result<std::unique_ptr<Shader>> createShader(const ShaderDesc& desc) override {
        std::unique_ptr<Shader> shader = std::make_unique<RegistryTestShader>(desc);
        return std::move(shader);
    }
    Result<std::unique_ptr<PipelineState>> createPipelineState(const GraphicsPipelineDesc& desc) override {
        ++pipeline_create_count_;
        std::unique_ptr<PipelineState> pipeline = std::make_unique<RegistryTestPipeline>(desc);
        return std::move(pipeline);
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

    ResultVoid uploadTextureData(Texture*, const TextureUploadDesc&) override {
        ++upload_count_;
        return {};
    }
    ResultVoid beginUploadBatch() override { return {}; }
    ResultVoid flushUploadBatch() override { return {}; }
    Result<SubmissionToken> executeCommandLists(CommandList**, uint32_t, Fence*, uint64_t) override {
        return issueSubmission();
    }
    ResultVoid waitIdle() override { return {}; }
    Result<CommandList*> beginFrame(SwapChain*) override { return static_cast<CommandList*>(nullptr); }
    Result<SubmissionToken> endFrame(SwapChain*) override { return issueSubmission(); }

private:
    RegistryTestFence* fence_ = nullptr;
    GPUDeviceCapabilities capabilities_;
    RenderConfig render_config_;
    size_t create_count_ = 0;
    size_t upload_count_ = 0;
    size_t live_texture_count_ = 0;
    size_t pipeline_create_count_ = 0;
    bool fail_next_buffer_create_ = false;
    bool fail_next_texture_create_ = false;
};

graphics::Mesh makeRegistryJournalMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::wire();
    mesh.vertices.resize(mesh.layout.stride());
    return mesh;
}

TEST(AssetGpuRegistryTests, RevisionChangesOnlyForRealGeometryMutations) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device);
    EXPECT_NE(registry.revision(), 0u);

    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::wire();
    mesh.vertices.resize(mesh.layout.stride());
    const AssetGpuKey key = makeAssetGpuKey(6);

    const uint64_t initialRevision = registry.revision();
    auto first = registry.acquireGeometry(key, mesh);
    ASSERT_TRUE(first);
    ASSERT_NE(*first, nullptr);
    ASSERT_TRUE((*first)->isValid());
    EXPECT_EQ(registry.revision(), initialRevision + 1);

    const uint64_t addedRevision = registry.revision();
    Buffer* const firstBuffer = (*first)->vertexBuffer.get();
    auto reused = registry.acquireGeometry(key, mesh);
    ASSERT_TRUE(reused);
    EXPECT_EQ(*reused, *first);
    EXPECT_EQ((*reused)->vertexBuffer.get(), firstBuffer);
    EXPECT_EQ(registry.revision(), addedRevision);

    auto replaced = registry.acquireGeometry(key, mesh, true);
    ASSERT_TRUE(replaced);
    ASSERT_NE((*replaced)->vertexBuffer.get(), firstBuffer);
    EXPECT_EQ(registry.revision(), addedRevision + 1);

    const uint64_t replacedRevision = registry.revision();
    auto retired = registry.retireGeometry(key);
    ASSERT_TRUE(retired);
    EXPECT_TRUE(*retired);
    EXPECT_EQ(registry.revision(), replacedRevision + 1);

    const uint64_t retiredRevision = registry.revision();
    auto repeated = registry.retireGeometry(key);
    ASSERT_TRUE(repeated);
    EXPECT_FALSE(*repeated);
    EXPECT_EQ(registry.revision(), retiredRevision);
}

TEST(AssetGpuRegistryChangeJournalTests, SupportsIndependentConsumersAndStructuredGeometryEvents) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device, 8);
    const AssetGpuChangeCursor firstStart = registry.currentChangeCursor();
    const AssetGpuChangeCursor secondStart = registry.currentChangeCursor();
    const RenderResourceKey key = makeAssetGpuKey(60);
    const graphics::Mesh mesh = makeRegistryJournalMesh();

    ASSERT_TRUE(registry.acquireGeometry(key, mesh));
    const AssetGpuChangeSet firstAdd = registry.readChanges(firstStart);
    const AssetGpuChangeSet repeatedAddRead = registry.readChanges(firstStart);
    ASSERT_TRUE(firstAdd.hasChanges());
    ASSERT_EQ(firstAdd.changes.size(), 1u);
    ASSERT_EQ(repeatedAddRead.changes.size(), 1u);
    EXPECT_EQ(firstAdd.changes.front().kind, AssetGpuChangeKind::GeometryUpserted);
    EXPECT_EQ(firstAdd.changes.front().key, key);
    EXPECT_EQ(repeatedAddRead.changes.front().revision, firstAdd.changes.front().revision);

    AssetGpuChangeCursor firstCursor = firstAdd.cursorAfterApply();
    ASSERT_TRUE(registry.acquireGeometry(key, mesh));
    EXPECT_EQ(registry.readChanges(firstCursor).status, AssetGpuChangeStatus::UpToDate);

    ASSERT_TRUE(registry.acquireGeometry(key, mesh, true));
    const AssetGpuChangeSet replacement = registry.readChanges(firstCursor);
    ASSERT_TRUE(replacement.hasChanges());
    ASSERT_EQ(replacement.changes.size(), 1u);
    EXPECT_EQ(replacement.changes.front().kind, AssetGpuChangeKind::GeometryUpserted);
    EXPECT_EQ(replacement.changes.front().key, key);
    firstCursor = replacement.cursorAfterApply();

    // 第一个消费者的读取不影响第二个消费者，后者仍能看到新增与替换。
    const AssetGpuChangeSet secondBeforeRetire = registry.readChanges(secondStart);
    ASSERT_TRUE(secondBeforeRetire.hasChanges());
    ASSERT_EQ(secondBeforeRetire.changes.size(), 2u);

    auto retired = registry.retireGeometry(key);
    ASSERT_TRUE(retired);
    ASSERT_TRUE(*retired);
    const AssetGpuChangeSet retirement = registry.readChanges(firstCursor);
    ASSERT_TRUE(retirement.hasChanges());
    ASSERT_EQ(retirement.changes.size(), 1u);
    EXPECT_EQ(retirement.changes.front().kind, AssetGpuChangeKind::GeometryRetired);
    EXPECT_EQ(retirement.changes.front().key, key);

    const AssetGpuChangeSet secondAfterRetire = registry.readChanges(secondStart);
    ASSERT_TRUE(secondAfterRetire.hasChanges());
    ASSERT_EQ(secondAfterRetire.changes.size(), 3u);
    EXPECT_EQ(secondAfterRetire.changes.back().kind, AssetGpuChangeKind::GeometryRetired);
}

TEST(AssetGpuRegistryChangeJournalTests, TextureChangesPublishButInternalTexturesDoNotCreateJournalGaps) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device, 8);
    const AssetGpuChangeCursor start = registry.currentChangeCursor();
    const uint64_t revisionBeforeInternal = registry.revision();

    Texture* internal = registry.createTexture(1, 1, TextureFormat::RGBA8_UNorm, TextureUsageFlags::ShaderResource,
                                               "journal-internal");
    ASSERT_NE(internal, nullptr);
    EXPECT_EQ(registry.revision(), revisionBeforeInternal + 1u);
    EXPECT_EQ(registry.currentChangeCursor().revision, start.revision);
    EXPECT_EQ(registry.readChanges(start).status, AssetGpuChangeStatus::UpToDate);

    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);
    const RenderResourceKey key = makeAssetGpuKey(61);
    ASSERT_TRUE(registry.acquireTexture(key, *image, {}, 1));
    const AssetGpuChangeSet added = registry.readChanges(start);
    ASSERT_TRUE(added.hasChanges());
    ASSERT_EQ(added.changes.size(), 1u);
    EXPECT_EQ(added.changes.front().kind, AssetGpuChangeKind::TextureUpserted);
    EXPECT_EQ(added.changes.front().key, key);

    AssetGpuChangeCursor cursor = added.cursorAfterApply();
    ASSERT_TRUE(registry.acquireTexture(key, *image, {}, 1));
    EXPECT_EQ(registry.readChanges(cursor).status, AssetGpuChangeStatus::UpToDate);

    ASSERT_TRUE(registry.acquireTexture(key, *image, {}, 2));
    const AssetGpuChangeSet replaced = registry.readChanges(cursor);
    ASSERT_TRUE(replaced.hasChanges());
    ASSERT_EQ(replaced.changes.size(), 1u);
    EXPECT_EQ(replaced.changes.front().kind, AssetGpuChangeKind::TextureUpserted);
    cursor = replaced.cursorAfterApply();

    auto retired = registry.retireTexture(key);
    ASSERT_TRUE(retired);
    ASSERT_TRUE(*retired);
    const AssetGpuChangeSet removed = registry.readChanges(cursor);
    ASSERT_TRUE(removed.hasChanges());
    ASSERT_EQ(removed.changes.size(), 1u);
    EXPECT_EQ(removed.changes.front().kind, AssetGpuChangeKind::TextureRetired);
    EXPECT_EQ(removed.changes.front().key, key);
}

TEST(AssetGpuRegistryChangeJournalTests, ClearPublishesOneInvalidateAllAndNoOpClearPublishesNothing) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device, 8);
    const graphics::Mesh mesh = makeRegistryJournalMesh();
    ASSERT_TRUE(registry.acquireGeometry(makeAssetGpuKey(62), mesh));
    ASSERT_TRUE(registry.acquireGeometry(makeAssetGpuKey(63), mesh));
    const AssetGpuChangeCursor beforeClear = registry.currentChangeCursor();
    const uint64_t revisionBeforeClear = registry.revision();

    registry.clear();
    EXPECT_EQ(registry.revision(), revisionBeforeClear + 1u);
    const AssetGpuChangeSet cleared = registry.readChanges(beforeClear);
    ASSERT_TRUE(cleared.hasChanges());
    ASSERT_EQ(cleared.changes.size(), 1u);
    EXPECT_EQ(cleared.changes.front().kind, AssetGpuChangeKind::InvalidateAll);
    EXPECT_FALSE(static_cast<bool>(cleared.changes.front().key));

    const AssetGpuChangeCursor afterClear = cleared.cursorAfterApply();
    registry.clear();
    EXPECT_EQ(registry.readChanges(afterClear).status, AssetGpuChangeStatus::UpToDate);
}

TEST(AssetGpuRegistryChangeJournalTests, DetectsOverflowForeignAheadAndUnboundCursors) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device, 2);
    const AssetGpuChangeCursor start = registry.currentChangeCursor();
    const graphics::Mesh mesh = makeRegistryJournalMesh();

    ASSERT_TRUE(registry.acquireGeometry(makeAssetGpuKey(64), mesh));
    const AssetGpuChangeCursor afterFirst = registry.currentChangeCursor();
    ASSERT_TRUE(registry.acquireGeometry(makeAssetGpuKey(65), mesh));
    ASSERT_TRUE(registry.acquireGeometry(makeAssetGpuKey(66), mesh));
    EXPECT_TRUE(registry.readChanges(start).requiresFullResync());

    const AssetGpuChangeSet retainedBoundary = registry.readChanges(afterFirst);
    ASSERT_TRUE(retainedBoundary.hasChanges());
    EXPECT_EQ(retainedBoundary.changes.size(), 2u);

    AssetGpuRegistry otherRegistry(device, 2);
    EXPECT_TRUE(otherRegistry.readChanges(registry.currentChangeCursor()).requiresFullResync());
    const AssetGpuChangeCursor ahead{ registry.changeDomain(), registry.currentChangeCursor().revision + 1u };
    EXPECT_TRUE(registry.readChanges(ahead).requiresFullResync());
    EXPECT_TRUE(registry.readChanges({ 0, 1 }).requiresFullResync());

    AssetGpuRegistry zeroCapacityRegistry(device, 0);
    const AssetGpuChangeCursor zeroStart = zeroCapacityRegistry.currentChangeCursor();
    ASSERT_TRUE(zeroCapacityRegistry.acquireGeometry(makeAssetGpuKey(67), mesh));
    EXPECT_TRUE(zeroCapacityRegistry.readChanges(zeroStart).requiresFullResync());
}

TEST(AssetGpuRegistryChangeJournalTests, FailedAndNoOpOperationsDoNotPublish) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device, 8);
    const graphics::Mesh mesh = makeRegistryJournalMesh();
    const RenderResourceKey geometryKey = makeAssetGpuKey(68);
    const AssetGpuChangeCursor start = registry.currentChangeCursor();
    const uint64_t initialRevision = registry.revision();

    device.failNextBufferCreate();
    EXPECT_FALSE(registry.acquireGeometry(geometryKey, mesh));
    EXPECT_EQ(registry.revision(), initialRevision);
    EXPECT_EQ(registry.readChanges(start).status, AssetGpuChangeStatus::UpToDate);

    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);
    device.failNextTextureCreate();
    EXPECT_FALSE(registry.acquireTexture(makeAssetGpuKey(69), *image, {}, 1));
    EXPECT_EQ(registry.readChanges(start).status, AssetGpuChangeStatus::UpToDate);

    EXPECT_FALSE(registry.acquireGeometry({}, mesh));
    EXPECT_EQ(registry.readChanges(start).status, AssetGpuChangeStatus::UpToDate);

    ASSERT_TRUE(registry.acquireGeometry(geometryKey, mesh));
    const AssetGpuChangeCursor afterAdd = registry.currentChangeCursor();
    const uint64_t revisionAfterAdd = registry.revision();
    ASSERT_TRUE(registry.acquireGeometry(geometryKey, mesh));
    auto missingRetire = registry.retireGeometry(makeAssetGpuKey(70));
    ASSERT_TRUE(missingRetire);
    EXPECT_FALSE(*missingRetire);
    EXPECT_EQ(registry.revision(), revisionAfterAdd);
    EXPECT_EQ(registry.readChanges(afterAdd).status, AssetGpuChangeStatus::UpToDate);

    device.failNextBufferCreate();
    EXPECT_FALSE(registry.acquireGeometry(geometryKey, mesh, true));
    EXPECT_EQ(registry.revision(), revisionAfterAdd);
    EXPECT_EQ(registry.readChanges(afterAdd).status, AssetGpuChangeStatus::UpToDate);
}

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

TEST(AssetGpuRegistryTests, RevisionChangesOnlyForRealTextureMutationsAndNonEmptyClear) {
    RegistryTestDevice device;
    AssetGpuRegistry registry(device);
    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);

    const AssetGpuKey firstKey = makeAssetGpuKey(10);
    const AssetGpuKey secondKey = makeAssetGpuKey(11);
    const uint64_t initialRevision = registry.revision();
    registry.clear();
    EXPECT_EQ(registry.revision(), initialRevision);

    ASSERT_TRUE(registry.acquireTexture(firstKey, *image, {}, 1));
    EXPECT_EQ(registry.revision(), initialRevision + 1);

    const uint64_t addedRevision = registry.revision();
    ASSERT_TRUE(registry.acquireTexture(firstKey, *image, {}, 1));
    EXPECT_EQ(registry.revision(), addedRevision);

    ASSERT_TRUE(registry.acquireTexture(firstKey, *image, {}, 2));
    EXPECT_EQ(registry.revision(), addedRevision + 1);

    const uint64_t replacedRevision = registry.revision();
    auto missing = registry.retireTexture(secondKey);
    ASSERT_TRUE(missing);
    EXPECT_FALSE(*missing);
    EXPECT_EQ(registry.revision(), replacedRevision);

    ASSERT_TRUE(registry.acquireTexture(secondKey, *image, {}, 1));
    const uint64_t beforeClear = registry.revision();
    registry.clear();
    EXPECT_EQ(registry.textureCount(), 0u);
    EXPECT_EQ(registry.revision(), beforeClear + 1);

    const uint64_t clearedRevision = registry.revision();
    registry.clear();
    EXPECT_EQ(registry.revision(), clearedRevision);
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

TEST(DeviceResourceServiceTests, SharedClientOwnershipRetiresOnlyAfterTheLastClient) {
    RegistryTestDevice device;
    DeviceResourceService resources(device);
    ASSERT_TRUE(resources.init());
    const size_t defaultResourceCreateCount = device.createCount();
    ASSERT_TRUE(resources.init());
    EXPECT_EQ(device.createCount(), defaultResourceCreateCount);

    const DeviceResourceClientId firstClient = resources.registerClient();
    const DeviceResourceClientId secondClient = resources.registerClient();
    const ResourceDomainId domain = resourceDomainForAssetLibrary(42);
    const RenderResourceKey key = makeRenderResourceKey(domain, 7, RenderResourceKind::Texture);
    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);
    RenderResourcePrepareList prepare;
    prepare.addTexture(RenderTextureResourceKey{ .resourceKey = key }, image, 1);
    ASSERT_TRUE(resources.preparePersistentResources(firstClient, prepare));
    ASSERT_TRUE(resources.preparePersistentResources(secondClient, prepare));
    EXPECT_EQ(resources.stats().clientCount, 2u);
    EXPECT_EQ(resources.stats().domainCount, 1u);
    EXPECT_EQ(resources.stats().textureCount, 1u);

    ASSERT_TRUE(resources.releaseClient(firstClient));
    EXPECT_EQ(resources.stats().clientCount, 1u);
    EXPECT_EQ(resources.stats().textureCount, 1u);
    EXPECT_NE(resources.assets().findTexture(key), nullptr);

    const SubmissionToken inFlight = device.issueSubmission();
    ASSERT_TRUE(resources.releaseClient(secondClient));
    EXPECT_EQ(resources.stats().clientCount, 0u);
    EXPECT_EQ(resources.stats().domainCount, 0u);
    EXPECT_EQ(resources.stats().textureCount, 0u);
    EXPECT_EQ(resources.assets().findTexture(key), nullptr);
    device.collectGarbage();
    EXPECT_GT(device.liveTextureCount(), 0u);
    device.complete(inFlight);
    device.collectGarbage();
}

TEST(DeviceResourceServiceTests, EqualSourcesInDifferentDomainsRemainIndependent) {
    RegistryTestDevice device;
    DeviceResourceService resources(device);
    ASSERT_TRUE(resources.init());
    const DeviceResourceClientId firstClient = resources.registerClient();
    const DeviceResourceClientId secondClient = resources.registerClient();
    const RenderResourceKey firstKey =
            makeRenderResourceKey(resourceDomainForAssetLibrary(1), 9, RenderResourceKind::Texture);
    const RenderResourceKey secondKey =
            makeRenderResourceKey(resourceDomainForAssetLibrary(2), 9, RenderResourceKind::Texture);
    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    RenderResourcePrepareList firstPrepare;
    RenderResourcePrepareList secondPrepare;
    firstPrepare.addTexture(RenderTextureResourceKey{ .resourceKey = firstKey }, image, 1);
    secondPrepare.addTexture(RenderTextureResourceKey{ .resourceKey = secondKey }, image, 1);
    ASSERT_TRUE(resources.preparePersistentResources(firstClient, firstPrepare));
    ASSERT_TRUE(resources.preparePersistentResources(secondClient, secondPrepare));
    EXPECT_EQ(resources.stats().domainCount, 2u);
    EXPECT_EQ(resources.stats().textureCount, 2u);
    ASSERT_TRUE(resources.releaseClient(firstClient));
    EXPECT_EQ(resources.stats().textureCount, 1u);
    EXPECT_EQ(resources.assets().findTexture(firstKey), nullptr);
    EXPECT_NE(resources.assets().findTexture(secondKey), nullptr);
    ASSERT_TRUE(resources.releaseClient(secondClient));
}

TEST(DeviceResourceServiceTests, PipelineLibraryUsesTheCompleteTargetSignature) {
    RegistryTestDevice device;
    DeviceResourceService resources(device);
    ASSERT_TRUE(resources.init());
    DevicePipelineKey key{
        .technique = RenderTechnique::SolidLit,
        .colorFormat = TextureFormat::BGRA8_UNorm,
        .depthFormat = TextureFormat::D24_UNorm_S8_UInt,
        .sampleCount = 1,
        .hasDepth = true,
    };

    PipelineState* first = resources.pipelines().acquire(key);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(resources.pipelines().acquire(key), first);
    EXPECT_EQ(device.pipelineCreateCount(), 1u);

    key.objectBindingMode = ObjectBindingMode::InstancedBatch;
    PipelineState* instanced = resources.pipelines().acquire(key);
    ASSERT_NE(instanced, nullptr);
    EXPECT_NE(instanced, first);
    EXPECT_EQ(resources.pipelines().acquire(key), instanced);
    EXPECT_EQ(device.pipelineCreateCount(), 2u);

    key.objectBindingMode = ObjectBindingMode::Single;
    key.sampleCount = 4;
    PipelineState* multisampled = resources.pipelines().acquire(key);
    ASSERT_NE(multisampled, nullptr);
    EXPECT_NE(multisampled, first);
    key.hasDepth = false;
    PipelineState* depthless = resources.pipelines().acquire(key);
    ASSERT_NE(depthless, nullptr);
    EXPECT_NE(depthless, multisampled);
    EXPECT_EQ(device.pipelineCreateCount(), 4u);
    EXPECT_EQ(resources.stats().pipelineCount, 4u);
}

TEST(DeviceResourceServiceTests, TextStageIsSharedByCompleteTargetSignature) {
    RegistryTestDevice device;
    DeviceResourceService resources(device);
    ASSERT_TRUE(resources.init());

    RenderTargetInfo target;
    target.colorFormat = TextureFormat::BGRA8_UNorm;
    target.depthFormat = TextureFormat::D24_UNorm_S8_UInt;
    target.hasDepth = true;
    target.sampleCount = 1;

    TextStage* first = resources.acquireTextStage(target);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(resources.acquireTextStage(target), first);
    EXPECT_EQ(resources.stats().textStageCount, 1u);

    target.sampleCount = 4;
    TextStage* multisampled = resources.acquireTextStage(target);
    ASSERT_NE(multisampled, nullptr);
    EXPECT_NE(multisampled, first);
    EXPECT_EQ(resources.stats().textStageCount, 2u);
}

TEST(LightingPolicyTests, EmptyScenePreservesLegacyViewerLight) {
    const LightEnvironment environment;
    const ResolvedLighting resolved = resolveLighting(environment, math::Mat4{ 1.0 });

    ASSERT_EQ(resolved.lightCount, 1u);
    EXPECT_EQ(resolved.lights[0].type, LightType::Directional);
    const math::Vec3 expectedDirection = math::Vec3(0.35, -0.45, -0.82).normalized();
    EXPECT_NEAR(resolved.lights[0].direction.x, expectedDirection.x, 1.0e-12);
    EXPECT_NEAR(resolved.lights[0].direction.y, expectedDirection.y, 1.0e-12);
    EXPECT_NEAR(resolved.lights[0].direction.z, expectedDirection.z, 1.0e-12);
    EXPECT_EQ(resolved.lights[0].color, math::Vec3(0.95, 0.94, 0.92));
    EXPECT_DOUBLE_EQ(resolved.lights[0].intensity, 1.0);
    EXPECT_EQ(resolved.ambientColor, environment.ambientColor * environment.ambientIntensity);
}

TEST(LightingPolicyTests, ExplicitSceneLightSuppressesViewerFallback) {
    LightEnvironment environment;
    environment.addLight(Light::point(math::Vec3(1.0, 2.0, 3.0), 20.0, math::Vec3(0.2, 0.4, 0.8), 12.0));

    const ResolvedLighting resolved = resolveLighting(environment, math::Mat4{ 1.0 });
    ASSERT_EQ(resolved.lightCount, 1u);
    EXPECT_EQ(resolved.lights[0].type, LightType::Point);
    EXPECT_EQ(resolved.lights[0].position, math::Vec3(1.0, 2.0, 3.0));
}

TEST(LightingPolicyTests, ExplicitZeroAmbientRemainsDisabled) {
    LightEnvironment environment;
    environment.ambientColor = math::Vec3(0.0);
    environment.ambientIntensity = 0.0;

    const ResolvedLighting resolved = resolveLighting(environment, math::Mat4{ 1.0 });
    EXPECT_EQ(resolved.ambientColor, math::Vec3(0.0));
}

TEST(LightingPolicyTests, ModesHaveExplicitAndBoundedComposition) {
    LightEnvironment environment;
    environment.addLight(Light::point(math::Vec3(1.0), 5.0));

    environment.mode = LightingMode::ViewerDefault;
    ResolvedLighting resolved = resolveLighting(environment, math::Mat4{ 1.0 });
    ASSERT_EQ(resolved.lightCount, 1u);
    EXPECT_EQ(resolved.lights[0].type, LightType::Directional);

    environment.mode = LightingMode::Hybrid;
    resolved = resolveLighting(environment, math::Mat4{ 1.0 });
    ASSERT_EQ(resolved.lightCount, 2u);
    EXPECT_EQ(resolved.lights[0].type, LightType::Directional);
    EXPECT_EQ(resolved.lights[1].type, LightType::Point);

    environment.clearLights();
    environment.mode = LightingMode::SceneOnly;
    resolved = resolveLighting(environment, math::Mat4{ 1.0 });
    EXPECT_EQ(resolved.lightCount, 0u);
}

TEST(LightingPolicyTests, InvalidValuesAreSanitizedBeforeSnapshotAndGpuPacking) {
    Light invalid;
    invalid.type = LightType::Spot;
    invalid.color = { -1.0, std::numeric_limits<double>::quiet_NaN(), 2.0 };
    invalid.direction = { std::numeric_limits<double>::infinity(), 0.0, 0.0 };
    invalid.position = { std::numeric_limits<double>::max(), 0.0, 0.0 };
    invalid.intensity = -4.0;
    invalid.range = -10.0;
    invalid.innerConeAngle = 2.0;
    invalid.outerConeAngle = -1.0;

    const Light sanitized = invalid.sanitized();
    EXPECT_EQ(sanitized.color, math::Vec3(0.0, 0.0, 2.0));
    EXPECT_EQ(sanitized.direction, math::Vec3(0.0, 0.0, -1.0));
    EXPECT_DOUBLE_EQ(sanitized.intensity, 0.0);
    EXPECT_DOUBLE_EQ(sanitized.range, 0.0);
    EXPECT_DOUBLE_EQ(sanitized.innerConeAngle, 0.0);
    EXPECT_DOUBLE_EQ(sanitized.outerConeAngle, 0.0);

    const SceneLightUniform gpu = makeSceneLightUniform(invalid);
    EXPECT_FLOAT_EQ(gpu.color[0], 0.0f);
    EXPECT_FLOAT_EQ(gpu.color[1], 0.0f);
    EXPECT_FLOAT_EQ(gpu.color[2], 2.0f);
    EXPECT_EQ(gpu.type, static_cast<uint32_t>(LightType::Spot));
    EXPECT_TRUE(std::isfinite(gpu.position[0]));
    EXPECT_FLOAT_EQ(gpu.position[0], std::numeric_limits<float>::max());
}

TEST(LightingPolicyTests, HybridNeverExceedsGpuLightCapacity) {
    LightEnvironment environment;
    environment.mode = LightingMode::Hybrid;
    for (uint32_t index = 0; index < LightEnvironment::kMaxLights; ++index)
        environment.addLight(Light::point(math::Vec3(static_cast<double>(index)), 10.0));

    const ResolvedLighting resolved = resolveLighting(environment, math::Mat4{ 1.0 });
    ASSERT_EQ(resolved.lightCount, LightEnvironment::kMaxLights);
    EXPECT_EQ(resolved.lights[0].type, LightType::Directional);
}

}  // namespace
}  // namespace mulan::engine
