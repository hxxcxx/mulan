/**
 * @file render_compiler_tests.cpp
 * @brief 验证 RenderCompiler 的增量 Packet 缓存、保守剔除与失败事务语义。
 * @author hxxcxx
 * @date 2026-07-16
 */

#include <mulan/render/asset_gpu_registry.h>
#include <mulan/render/backend/render_compiler.h>
#include <mulan/render/frontend/render_world.h>
#include <mulan/render/material/material_cache.h>
#include <mulan/rhi/device.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace mulan::engine {
namespace {

class CompilerTestBuffer final : public Buffer {
public:
    explicit CompilerTestBuffer(BufferDesc desc) : desc_(std::move(desc)) { desc_.discardInitialData(); }

    const BufferDesc& desc() const override { return desc_; }
    ResultVoid write(uint32_t, uint32_t, const void*) override { return {}; }
    ResultVoid readback(uint32_t, uint32_t, void*) override { return {}; }

private:
    BufferDesc desc_;
};

class CompilerTestTexture final : public Texture {
public:
    explicit CompilerTestTexture(TextureDesc desc) : desc_(std::move(desc)) {}

    const TextureDesc& desc() const override { return desc_; }

private:
    TextureDesc desc_;
};

class CompilerTestPipeline final : public PipelineState {
public:
    explicit CompilerTestPipeline(graphics::PrimitiveTopology topology) {
        desc_.name = "RenderCompilerTests";
        desc_.vertexLayout = graphics::layouts::surface();
        desc_.topology = topology;
    }

    const GraphicsPipelineDesc& desc() const override { return desc_; }

private:
    GraphicsPipelineDesc desc_;
};

class CompilerTestDevice final : public RHIDevice {
public:
    GraphicsBackend backend() const override { return GraphicsBackend::Vulkan; }
    const GPUDeviceCapabilities& capabilities() const override { return capabilities_; }
    const RenderConfig& renderConfig() const override { return render_config_; }
    math::Mat4 clipSpaceCorrectionMatrix() const override { return math::Mat4(1.0); }

    Result<std::unique_ptr<Buffer>> createBuffer(const BufferDesc& desc) override {
        std::unique_ptr<Buffer> buffer = std::make_unique<CompilerTestBuffer>(desc);
        return std::move(buffer);
    }
    Result<std::unique_ptr<Texture>> createTexture(const TextureDesc& desc) override {
        std::unique_ptr<Texture> texture = std::make_unique<CompilerTestTexture>(desc);
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

    ResultVoid uploadTextureData(Texture*, const TextureUploadDesc&) override { return {}; }
    ResultVoid beginUploadBatch() override { return {}; }
    ResultVoid flushUploadBatch() override { return {}; }
    Result<SubmissionToken> executeCommandLists(CommandList**, uint32_t, Fence*, uint64_t) override {
        return SubmissionToken{};
    }
    ResultVoid waitIdle() override { return {}; }
    Result<CommandList*> beginFrame(SwapChain*) override { return static_cast<CommandList*>(nullptr); }
    Result<SubmissionToken> endFrame(SwapChain*) override { return SubmissionToken{}; }

private:
    GPUDeviceCapabilities capabilities_;
    RenderConfig render_config_;
};

graphics::Mesh makeEdgeMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::position3();
    mesh.topology = graphics::PrimitiveTopology::LineList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 2u);
    return mesh;
}

graphics::Mesh makeSurfaceMesh() {
    graphics::Mesh mesh;
    mesh.layout = graphics::layouts::surface();
    mesh.topology = graphics::PrimitiveTopology::TriangleList;
    mesh.vertices.resize(static_cast<size_t>(mesh.layout.stride()) * 3u);
    return mesh;
}

RenderGeometryDesc makeEdgeGeometryDesc(RenderResourceKey key) {
    RenderGeometryDesc desc;
    desc.resourceKey = key;
    desc.topology = graphics::PrimitiveTopology::LineList;
    desc.vertexLayout = graphics::layouts::position3();
    desc.empty = false;
    return desc;
}

RenderGeometryDesc makeSurfaceGeometryDesc(RenderResourceKey key) {
    RenderGeometryDesc desc;
    desc.resourceKey = key;
    desc.topology = graphics::PrimitiveTopology::TriangleList;
    desc.vertexLayout = graphics::layouts::surface();
    desc.empty = false;
    return desc;
}

math::AABB3 boundsAt(double x) {
    return math::AABB3(math::Point3(x - 0.1, -0.1, -0.1), math::Point3(x + 0.1, 0.1, 0.1));
}

RenderObjectDesc makeEdgeObjectDesc(GeometryHandle geometry, uint32_t pickId, const math::AABB3& bounds,
                                    RenderBucket bucket = RenderBucket::Edge) {
    RenderObjectDesc desc;
    desc.pickId = PickId::fromValue(pickId);
    desc.worldBounds = bounds;
    desc.drawables.push_back({ .geometry = geometry, .bucket = bucket });
    return desc;
}

RenderObjectDesc makeSurfaceObjectDesc(GeometryHandle geometry, RenderMaterialHandle material, uint32_t pickId,
                                       const math::AABB3& bounds) {
    RenderObjectDesc desc;
    desc.pickId = PickId::fromValue(pickId);
    desc.worldBounds = bounds;
    desc.drawables.push_back({ .geometry = geometry, .material = material, .bucket = RenderBucket::Surface });
    return desc;
}

RenderViewDesc identityView() {
    RenderViewDesc view;
    view.width = 800;
    view.height = 600;
    return view;
}

const MeshDrawCommand* findCommandByPickId(std::span<const MeshDrawCommand> commands, uint32_t pickId) {
    const auto found = std::find_if(commands.begin(), commands.end(),
                                    [pickId](const MeshDrawCommand& command) { return command.pickId == pickId; });
    return found == commands.end() ? nullptr : &*found;
}

class RenderCompilerTests : public ::testing::Test {
protected:
    void prepareGeometry(RenderResourceKey key) { prepareGeometry(key, makeEdgeMesh()); }

    void prepareGeometry(RenderResourceKey key, const graphics::Mesh& mesh) {
        auto acquired = assets_.acquireGeometry(key, mesh);
        ASSERT_TRUE(acquired) << acquired.error().message;
        ASSERT_NE(*acquired, nullptr);
        ASSERT_TRUE((*acquired)->isValid());
    }

    RenderCompileContext makeContext() {
        RenderCompileContext context{ .assets = assets_, .materials = materials_ };
        context.surfacePipeline = &surface_pipeline_;
        context.highlightSurfacePipeline = &surface_pipeline_;
        context.edgePipeline = &edge_pipeline_;
        context.highlightEdgePipeline = &edge_pipeline_;
        return context;
    }

    CompilerTestDevice device_;
    AssetGpuRegistry assets_{ device_ };
    MaterialCache materials_;
    CompilerTestPipeline surface_pipeline_{ graphics::PrimitiveTopology::TriangleList };
    CompilerTestPipeline edge_pipeline_{ graphics::PrimitiveTopology::LineList };
    RenderCompiler compiler_;
};

TEST_F(RenderCompilerTests, SurfacePipelineDoesNotDependOnMaterialSidednessOrTransformWinding) {
    const RenderResourceKey key = makeAssetGpuKey(99);
    prepareGeometry(key, makeSurfaceMesh());
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeSurfaceGeometryDesc(key));
    RenderMaterialDesc singleSided;
    singleSided.resourceKey = defaultRenderMaterialResourceKey();
    const RenderMaterialHandle singleMaterial = world.addMaterial(singleSided);
    RenderMaterialDesc doubleSided = singleSided;
    doubleSided.resourceKey = makeRenderResourceKey(ResourceDomainId{ 1 }, 2, RenderResourceKind::Material);
    doubleSided.material.doubleSided = true;
    const RenderMaterialHandle doubleMaterial = world.addMaterial(doubleSided);

    auto regular = makeSurfaceObjectDesc(geometry, singleMaterial, 90, boundsAt(-0.3));
    auto mirrored = makeSurfaceObjectDesc(geometry, singleMaterial, 91, boundsAt(-0.1));
    mirrored.worldTransform = math::Mat4::scale(math::Vec3(-1.0, 1.0, 1.0));
    auto singular = makeSurfaceObjectDesc(geometry, singleMaterial, 92, boundsAt(0.1));
    singular.worldTransform = math::Mat4::scale(math::Vec3(0.0, 1.0, 1.0));
    auto explicitDouble = makeSurfaceObjectDesc(geometry, doubleMaterial, 93, boundsAt(0.3));
    world.addObject(regular);
    world.addObject(mirrored);
    world.addObject(singular);
    world.addObject(explicitDouble);

    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    ASSERT_EQ(compiler_.surfaceCommands().size(), 4u);
    EXPECT_EQ(findCommandByPickId(compiler_.surfaceCommands(), 90)->pipelineState, &surface_pipeline_);
    EXPECT_EQ(findCommandByPickId(compiler_.surfaceCommands(), 91)->pipelineState, &surface_pipeline_);
    EXPECT_EQ(findCommandByPickId(compiler_.surfaceCommands(), 92)->pipelineState, &surface_pipeline_);
    EXPECT_EQ(findCommandByPickId(compiler_.surfaceCommands(), 93)->pipelineState, &surface_pipeline_);
}

TEST_F(RenderCompilerTests, ReusesEveryPacketForAnIdenticalSnapshot) {
    const RenderResourceKey key = makeAssetGpuKey(100);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    world.addObject(makeEdgeObjectDesc(geometry, 1, boundsAt(-0.25)));
    world.addObject(makeEdgeObjectDesc(geometry, 2, boundsAt(0.25)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_TRUE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 2u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 2u);
    EXPECT_TRUE(std::ranges::all_of(compiler_.edgeCommands(),
                                    [](const auto& command) { return command.batchInstancingEligible; }));

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_FALSE(stats.fullRebuild);
    EXPECT_TRUE(stats.cacheHit);
    EXPECT_EQ(stats.recompiledPacketCount, 0u);
    EXPECT_EQ(stats.reusedPacketCount, 2u);
    EXPECT_EQ(stats.assembledCommandCount, 2u);
    EXPECT_EQ(compiler_.edgeCommands().size(), 2u);
    EXPECT_TRUE(std::ranges::all_of(compiler_.edgeCommands(),
                                    [](const auto& command) { return command.batchInstancingEligible; }));
}

TEST_F(RenderCompilerTests, SingleObjectUpdateRecompilesOnlyItsPacketAndKeepsBothCommands) {
    const RenderResourceKey key = makeAssetGpuKey(101);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    const RenderObjectDesc firstDesc = makeEdgeObjectDesc(geometry, 11, boundsAt(-0.25));
    RenderObjectDesc secondDesc = makeEdgeObjectDesc(geometry, 12, boundsAt(0.25));
    world.addObject(firstDesc);
    const RenderObjectId second = world.addObject(secondDesc);
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    secondDesc.worldTransform = math::Mat4::translate(math::Vec3(0.5, 0.0, 0.0));
    ASSERT_TRUE(world.updateObject(second, secondDesc));
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));

    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_FALSE(stats.fullRebuild);
    EXPECT_FALSE(stats.cacheHit);
    EXPECT_EQ(stats.recompiledPacketCount, 1u);
    EXPECT_EQ(stats.reusedPacketCount, 1u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 2u);
    const MeshDrawCommand* updated = findCommandByPickId(compiler_.edgeCommands(), 12);
    ASSERT_NE(updated, nullptr);
    EXPECT_DOUBLE_EQ(updated->worldTransform[3].x, 0.5);
    EXPECT_NE(findCommandByPickId(compiler_.edgeCommands(), 11), nullptr);
}

TEST_F(RenderCompilerTests, CameraMovementReassemblesVisibilityWithoutRecompilingPackets) {
    const RenderResourceKey key = makeAssetGpuKey(102);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    world.addObject(makeEdgeObjectDesc(geometry, 21, boundsAt(0.0)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    view.viewMatrix = math::Mat4::translate(math::Vec3(-0.2, 0.0, 0.0));
    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));

    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_FALSE(stats.fullRebuild);
    EXPECT_TRUE(stats.cacheHit);
    EXPECT_EQ(stats.recompiledPacketCount, 0u);
    EXPECT_EQ(stats.reusedPacketCount, 1u);
    EXPECT_FALSE(stats.frustumFailOpen);
    EXPECT_EQ(stats.frustumVisibleObjectCount, 1u);
    EXPECT_EQ(compiler_.edgeCommands().size(), 1u);
}

TEST_F(RenderCompilerTests, HoverStateReassemblesHighlightsWithoutInvalidatingPackets) {
    const RenderResourceKey key = makeAssetGpuKey(107);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    world.addObject(makeEdgeObjectDesc(geometry, 22, boundsAt(0.0)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    RenderOptions options;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_TRUE(compiler_.highlightEdgeCommands().empty());

    options.hoveredPickId = PickId::fromValue(22);
    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_TRUE(stats.cacheHit);
    EXPECT_EQ(stats.recompiledPacketCount, 0u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    ASSERT_EQ(compiler_.highlightEdgeCommands().size(), 1u);
    EXPECT_FALSE(compiler_.edgeCommands().front().selected);
    EXPECT_FALSE(compiler_.edgeCommands().front().hovered);
    EXPECT_FALSE(compiler_.highlightEdgeCommands().front().selected);
    EXPECT_TRUE(compiler_.highlightEdgeCommands().front().hovered);
    EXPECT_TRUE(compiler_.edgeCommands().front().batchInstancingEligible);
    EXPECT_FALSE(compiler_.highlightEdgeCommands().front().batchInstancingEligible);
}

TEST_F(RenderCompilerTests, FrustumCullsOutsideBoundsAndInvalidMatrixFailsOpen) {
    const RenderResourceKey key = makeAssetGpuKey(103);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    world.addObject(makeEdgeObjectDesc(geometry, 31, boundsAt(0.0)));
    world.addObject(makeEdgeObjectDesc(geometry, 32, boundsAt(4.0)));
    world.addObject(makeEdgeObjectDesc(geometry, 33, math::AABB3::empty()));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc validView = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &validView, true));
    const RenderPacketCacheStats validStats = compiler_.lastPacketCacheStats();
    EXPECT_FALSE(validStats.frustumFailOpen);
    EXPECT_EQ(validStats.sourceVisibleObjectCount, 3u);
    EXPECT_EQ(validStats.uncullableObjectCount, 1u);
    EXPECT_EQ(validStats.frustumVisibleObjectCount, 2u);
    EXPECT_EQ(validStats.culledObjectCount, 1u);
    EXPECT_NE(findCommandByPickId(compiler_.edgeCommands(), 31), nullptr);
    EXPECT_EQ(findCommandByPickId(compiler_.edgeCommands(), 32), nullptr);
    EXPECT_NE(findCommandByPickId(compiler_.edgeCommands(), 33), nullptr);

    RenderViewDesc invalidView = validView;
    invalidView.projectionMatrix[0].x = std::numeric_limits<double>::quiet_NaN();
    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &invalidView, true));
    const RenderPacketCacheStats& invalidStats = compiler_.lastPacketCacheStats();
    EXPECT_TRUE(invalidStats.cacheHit);
    EXPECT_EQ(invalidStats.recompiledPacketCount, 0u);
    EXPECT_TRUE(invalidStats.frustumFailOpen);
    EXPECT_EQ(invalidStats.frustumVisibleObjectCount, 3u);
    EXPECT_EQ(invalidStats.culledObjectCount, 0u);
    EXPECT_EQ(compiler_.edgeCommands().size(), 3u);
}

TEST_F(RenderCompilerTests, OverlayCompilationNeverAppliesSceneFrustumCulling) {
    const RenderResourceKey key = makeAssetGpuKey(104);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    world.addObject(makeEdgeObjectDesc(geometry, 41, boundsAt(0.0), RenderBucket::OverlayEdge));
    world.addObject(makeEdgeObjectDesc(geometry, 42, boundsAt(10.0), RenderBucket::OverlayEdge));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, false));
    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_EQ(stats.sourceVisibleObjectCount, 2u);
    EXPECT_EQ(stats.frustumVisibleObjectCount, 2u);
    EXPECT_EQ(stats.culledObjectCount, 0u);
    EXPECT_FALSE(stats.frustumFailOpen);
    EXPECT_EQ(compiler_.edgeCommands().size(), 2u);
    EXPECT_TRUE(std::ranges::none_of(compiler_.edgeCommands(),
                                     [](const auto& command) { return command.batchInstancingEligible; }));
}

TEST_F(RenderCompilerTests, GeometryResolutionObservesAssetChangesWithoutCurrentPacketUsers) {
    const RenderResourceKey geometryKey = makeAssetGpuKey(108);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(geometryKey));
    const RenderObjectId firstObject = world.addObject(makeEdgeObjectDesc(geometry, 45, boundsAt(0.0)));
    RenderCompileContext context = makeContext();
    RenderOptions options;
    options.showEdges = false;
    const RenderViewDesc view = identityView();

    // 缺失资源在禁用 pass 中可以缓存；移除最后一个对象后 resolution 仍可能继续存活。
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    ASSERT_TRUE(world.removeObject(firstObject));
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));

    prepareGeometry(geometryKey);
    // 当前没有 Packet 用户，但 journal 事件仍必须通过独立索引驱逐 MissingGpu 缓存。
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 0u);

    world.addObject(makeEdgeObjectDesc(geometry, 46, boundsAt(0.0)));
    options.showEdges = true;
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 1u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    EXPECT_EQ(compiler_.edgeCommands().front().pickId, 46u);
}

TEST_F(RenderCompilerTests, TextureResolutionObservesAssetChangesWithoutCurrentPacketUsers) {
    const ResourceDomainId domain = resourceDomainForAssetLibrary(9001);
    const RenderResourceKey geometryKey = makeRenderResourceKey(domain, 1, RenderResourceKind::Geometry);
    const RenderResourceKey materialKey = makeRenderResourceKey(domain, 2, RenderResourceKind::Material);
    const RenderResourceKey textureKey = makeRenderResourceKey(domain, 3, RenderResourceKind::Texture);
    const auto image = core::Image::create(1, 1, core::PixelFormat::RGBA8);
    ASSERT_TRUE(image);

    prepareGeometry(geometryKey, makeSurfaceMesh());
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeSurfaceGeometryDesc(geometryKey));
    RenderMaterialDesc materialDesc;
    materialDesc.resourceKey = materialKey;
    materialDesc.baseColorTexture.resourceKey = textureKey;
    materialDesc.baseColorTexture.image = image;
    materialDesc.baseColorTexture.contentRevision = 1;
    const RenderMaterialHandle material = world.addMaterial(std::move(materialDesc));
    const RenderObjectId firstObject = world.addObject(makeSurfaceObjectDesc(geometry, material, 47, boundsAt(0.0)));
    RenderCompileContext context = makeContext();
    RenderOptions options;
    options.showSurfaces = false;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    ASSERT_TRUE(world.removeObject(firstObject));
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));

    auto prepared = assets_.acquireTexture(textureKey, *image, {}, 1);
    ASSERT_TRUE(prepared) << prepared.error().message;
    ASSERT_NE(*prepared, nullptr);
    Texture* preparedTexture = *prepared;
    // 与几何相同，零 Packet 用户期间的 TextureUpserted 也必须驱逐旧 MissingGpu 状态。
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 0u);

    world.addObject(makeSurfaceObjectDesc(geometry, material, 48, boundsAt(0.0)));
    options.showSurfaces = true;
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 1u);
    ASSERT_EQ(compiler_.surfaceCommands().size(), 1u);
    EXPECT_EQ(compiler_.surfaceCommands().front().pickId, 48u);
    EXPECT_EQ(compiler_.surfaceCommands().front().albedoTex, preparedTexture);
}

TEST_F(RenderCompilerTests, UnrelatedAssetChangesDoNotRecompileOrReassembleCommands) {
    const RenderResourceKey usedKey = makeAssetGpuKey(109);
    const RenderResourceKey unrelatedKey = makeAssetGpuKey(110);
    prepareGeometry(usedKey);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(usedKey));
    world.addObject(makeEdgeObjectDesc(geometry, 49, boundsAt(0.0)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    const uint64_t commandRevision = compiler_.commandRevision();
    prepareGeometry(unrelatedKey);

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_FALSE(stats.fullRebuild);
    EXPECT_TRUE(stats.cacheHit);
    EXPECT_TRUE(stats.assemblyCacheHit);
    EXPECT_EQ(stats.recompiledPacketCount, 0u);
    EXPECT_EQ(compiler_.commandRevision(), commandRevision);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    EXPECT_EQ(compiler_.edgeCommands().front().pickId, 49u);
}

TEST_F(RenderCompilerTests, GeometryAssetReplacementInvalidatesOnlyUsersForEveryConsumer) {
    const RenderResourceKey replacedKey = makeAssetGpuKey(111);
    const RenderResourceKey stableKey = makeAssetGpuKey(112);
    prepareGeometry(replacedKey);
    prepareGeometry(stableKey);
    RenderWorld world;
    const GeometryHandle replacedGeometry = world.addGeometry(makeEdgeGeometryDesc(replacedKey));
    const GeometryHandle stableGeometry = world.addGeometry(makeEdgeGeometryDesc(stableKey));
    world.addObject(makeEdgeObjectDesc(replacedGeometry, 61, boundsAt(-0.25)));
    world.addObject(makeEdgeObjectDesc(stableGeometry, 62, boundsAt(0.25)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();
    RenderCompiler secondCompiler;

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    ASSERT_TRUE(secondCompiler.compile(snapshot, options, context, &view, true));
    const MeshDrawCommand* oldReplaced = findCommandByPickId(compiler_.edgeCommands(), 61);
    const MeshDrawCommand* oldStable = findCommandByPickId(compiler_.edgeCommands(), 62);
    ASSERT_NE(oldReplaced, nullptr);
    ASSERT_NE(oldStable, nullptr);
    Buffer* const oldReplacedBuffer = oldReplaced->vertexBuffer;
    Buffer* const stableBuffer = oldStable->vertexBuffer;

    const graphics::Mesh replacementMesh = makeEdgeMesh();
    auto replaced = assets_.acquireGeometry(replacedKey, replacementMesh, true);
    ASSERT_TRUE(replaced) << replaced.error().message;
    ASSERT_NE(*replaced, nullptr);
    ASSERT_NE((*replaced)->vertexBuffer.get(), oldReplacedBuffer);

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 1u);
    EXPECT_EQ(compiler_.lastPacketCacheStats().reusedPacketCount, 1u);
    ASSERT_TRUE(secondCompiler.compile(snapshot, options, context, &view, true));
    EXPECT_FALSE(secondCompiler.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(secondCompiler.lastPacketCacheStats().recompiledPacketCount, 1u);
    EXPECT_EQ(secondCompiler.lastPacketCacheStats().reusedPacketCount, 1u);

    for (RenderCompiler* consumer : { &compiler_, &secondCompiler }) {
        const MeshDrawCommand* changed = findCommandByPickId(consumer->edgeCommands(), 61);
        const MeshDrawCommand* unchanged = findCommandByPickId(consumer->edgeCommands(), 62);
        ASSERT_NE(changed, nullptr);
        ASSERT_NE(unchanged, nullptr);
        EXPECT_EQ(changed->vertexBuffer, (*replaced)->vertexBuffer.get());
        EXPECT_EQ(unchanged->vertexBuffer, stableBuffer);
    }
}

TEST_F(RenderCompilerTests, MissingGeometryIsDeferredWhilePassIsDisabledAndFailsWhenEnabled) {
    const RenderResourceKey missingKey = makeAssetGpuKey(113);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(missingKey));
    world.addObject(makeEdgeObjectDesc(geometry, 63, boundsAt(0.0)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    RenderOptions options;
    options.showEdges = false;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_TRUE(compiler_.edgeCommands().empty());

    options.showEdges = true;
    const ResultVoid missing = compiler_.compile(snapshot, options, context, &view, true);
    EXPECT_FALSE(missing);
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 0u);
    EXPECT_EQ(compiler_.lastStats().missingGpuGeometryCount, 1u);

    prepareGeometry(missingKey);
    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 1u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    EXPECT_EQ(compiler_.edgeCommands().front().pickId, 63u);
}

TEST_F(RenderCompilerTests, MissingGeometryIsDeferredOutsideFrustumAndFailsOnEntry) {
    const RenderResourceKey missingKey = makeAssetGpuKey(114);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(missingKey));
    world.addObject(makeEdgeObjectDesc(geometry, 64, boundsAt(10.0)));
    const RenderWorldSnapshot snapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_TRUE(compiler_.edgeCommands().empty());
    EXPECT_EQ(compiler_.lastPacketCacheStats().culledObjectCount, 1u);

    view.viewMatrix = math::Mat4::translate(math::Vec3(-10.0, 0.0, 0.0));
    const ResultVoid missing = compiler_.compile(snapshot, options, context, &view, true);
    EXPECT_FALSE(missing);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 0u);
    EXPECT_EQ(compiler_.lastStats().missingGpuGeometryCount, 1u);

    prepareGeometry(missingKey);
    ASSERT_TRUE(compiler_.compile(snapshot, options, context, &view, true));
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 1u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    EXPECT_EQ(compiler_.edgeCommands().front().pickId, 64u);
}

TEST_F(RenderCompilerTests, RemovingObjectRetiresOnlyItsPacketAndCommand) {
    const RenderResourceKey key = makeAssetGpuKey(115);
    prepareGeometry(key);
    RenderWorld world;
    const GeometryHandle geometry = world.addGeometry(makeEdgeGeometryDesc(key));
    const RenderObjectId removed = world.addObject(makeEdgeObjectDesc(geometry, 65, boundsAt(-0.25)));
    world.addObject(makeEdgeObjectDesc(geometry, 66, boundsAt(0.25)));
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();

    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));
    ASSERT_TRUE(world.removeObject(removed));
    ASSERT_TRUE(compiler_.compile(world.snapshot(), options, context, &view, true));

    const RenderPacketCacheStats& stats = compiler_.lastPacketCacheStats();
    EXPECT_FALSE(stats.fullRebuild);
    EXPECT_FALSE(stats.cacheHit);
    EXPECT_EQ(stats.recompiledPacketCount, 0u);
    EXPECT_EQ(stats.reusedPacketCount, 1u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    EXPECT_EQ(compiler_.edgeCommands().front().pickId, 66u);
}

TEST_F(RenderCompilerTests, MissingGpuGeometryDoesNotPublishPartialPacketState) {
    const RenderResourceKey readyKey = makeAssetGpuKey(105);
    const RenderResourceKey missingKey = makeAssetGpuKey(106);
    prepareGeometry(readyKey);
    RenderWorld world;
    const GeometryHandle readyGeometry = world.addGeometry(makeEdgeGeometryDesc(readyKey));
    RenderObjectDesc readyDesc = makeEdgeObjectDesc(readyGeometry, 51, boundsAt(0.0));
    const RenderObjectId readyObject = world.addObject(readyDesc);
    const RenderWorldSnapshot committedSnapshot = world.snapshot();
    RenderCompileContext context = makeContext();
    const RenderOptions options;
    const RenderViewDesc view = identityView();
    ASSERT_TRUE(compiler_.compile(committedSnapshot, options, context, &view, true));

    // 先编译一个合法替换，再命中缺失资源，可以验证失败不会发布前半段更新。
    readyDesc.worldTransform = math::Mat4::translate(math::Vec3(0.75, 0.0, 0.0));
    ASSERT_TRUE(world.updateObject(readyObject, readyDesc));
    const GeometryHandle missingGeometry = world.addGeometry(makeEdgeGeometryDesc(missingKey));
    const RenderObjectId missingObject = world.addObject(makeEdgeObjectDesc(missingGeometry, 52, boundsAt(0.25)));
    const RenderWorldSnapshot failedSnapshot = world.snapshot();
    ASSERT_NE(failedSnapshot.object(missingObject), nullptr);
    size_t objectDifferenceCount = 0;
    failedSnapshot.forEachObjectDifference(
            committedSnapshot,
            [&](uint32_t, const RenderObjectRecord*, const RenderObjectRecord*) { ++objectDifferenceCount; });
    ASSERT_EQ(objectDifferenceCount, 2u);

    const ResultVoid failed = compiler_.compile(failedSnapshot, options, context, &view, true);
    EXPECT_FALSE(failed);
    EXPECT_EQ(compiler_.lastStats().missingGpuGeometryCount, 1u);
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);

    ASSERT_TRUE(compiler_.compile(committedSnapshot, options, context, &view, true));
    const RenderPacketCacheStats& rollbackStats = compiler_.lastPacketCacheStats();
    EXPECT_TRUE(rollbackStats.cacheHit);
    EXPECT_EQ(rollbackStats.recompiledPacketCount, 0u);
    EXPECT_EQ(compiler_.lastStats().missingGpuGeometryCount, 0u);
    EXPECT_EQ(compiler_.lastStats().missingGpuTextureCount, 0u);
    EXPECT_EQ(compiler_.lastStats().acceptedEdgeCommandCount, 1u);
    EXPECT_EQ(compiler_.lastWorkloadStats().visibleObjectCount, 1u);
    EXPECT_EQ(compiler_.lastWorkloadStats().edgeItemCount, 1u);
    ASSERT_EQ(compiler_.edgeCommands().size(), 1u);
    const MeshDrawCommand* committed = findCommandByPickId(compiler_.edgeCommands(), 51);
    ASSERT_NE(committed, nullptr);
    EXPECT_DOUBLE_EQ(committed->worldTransform[3].x, 0.0);

    prepareGeometry(missingKey);
    ASSERT_TRUE(compiler_.compile(failedSnapshot, options, context, &view, true));
    EXPECT_FALSE(compiler_.lastPacketCacheStats().fullRebuild);
    EXPECT_EQ(compiler_.lastPacketCacheStats().recompiledPacketCount, 2u);
    EXPECT_EQ(compiler_.edgeCommands().size(), 2u);
    const MeshDrawCommand* recovered = findCommandByPickId(compiler_.edgeCommands(), 51);
    ASSERT_NE(recovered, nullptr);
    EXPECT_DOUBLE_EQ(recovered->worldTransform[3].x, 0.75);
}

}  // namespace
}  // namespace mulan::engine
