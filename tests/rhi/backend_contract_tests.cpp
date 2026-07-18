/**
 * @file backend_contract_tests.cpp
 * @brief 四种 RHI 后端共享的设备与命令录制契约测试
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/rhi/device_factory.h>
#include <mulan/rhi/engine_error_code.h>

#if MULAN_TEST_HAS_RHI_D3D12
#include <mulan/rhi_dx12/backend.h>
#endif
#if MULAN_TEST_HAS_RHI_D3D11
#include <mulan/rhi_dx11/backend.h>
#endif
#if MULAN_TEST_HAS_RHI_VULKAN
#include <mulan/rhi_vulkan/backend.h>
#endif
#if MULAN_TEST_HAS_RHI_OPENGL
#include <mulan/rhi_opengl/backend.h>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace mulan::engine {
namespace {

using BackendModuleProvider = const BackendModule& (*) ();

struct BackendContractParam {
    const char* name = nullptr;
    BackendModuleProvider module = nullptr;
};

class ContractWindow {
public:
    ContractWindow() {
#if defined(_WIN32)
        instance_ = GetModuleHandleW(nullptr);
        static const wchar_t className[] = L"MulanRHIContractWindow";
        static const ATOM windowClass = [this] {
            WNDCLASSW desc{};
            desc.lpfnWndProc = DefWindowProcW;
            desc.hInstance = instance_;
            desc.lpszClassName = className;
            return RegisterClassW(&desc);
        }();
        if (windowClass != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            window_ = CreateWindowExW(0, className, L"Mulan RHI Contract", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                      CW_USEDEFAULT, 64, 64, nullptr, nullptr, instance_, nullptr);
        }
#endif
    }

    ~ContractWindow() {
#if defined(_WIN32)
        if (window_)
            DestroyWindow(window_);
#endif
    }

    ContractWindow(const ContractWindow&) = delete;
    ContractWindow& operator=(const ContractWindow&) = delete;

    NativeWindowHandle nativeHandle() const {
#if defined(_WIN32)
        return NativeWindowHandle::makeWin32(reinterpret_cast<uintptr_t>(instance_),
                                             reinterpret_cast<uintptr_t>(window_));
#else
        return {};
#endif
    }

    bool valid() const {
#if defined(_WIN32)
        return window_ != nullptr;
#else
        return true;
#endif
    }

private:
#if defined(_WIN32)
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
#endif
};

std::vector<BackendContractParam> availableBackends() {
    std::vector<BackendContractParam> backends;
#if MULAN_TEST_HAS_RHI_VULKAN
    backends.push_back({ "Vulkan", &vulkanBackendModule });
#endif
#if MULAN_TEST_HAS_RHI_D3D12
    backends.push_back({ "D3D12", &d3d12BackendModule });
#endif
#if MULAN_TEST_HAS_RHI_D3D11
    backends.push_back({ "D3D11", &d3d11BackendModule });
#endif
#if MULAN_TEST_HAS_RHI_OPENGL
    backends.push_back({ "OpenGL", &openGLBackendModule });
#endif
    return backends;
}

ResultVoid ensureBackendRegistered(const BackendModule& module) {
    auto& factory = DeviceFactory::instance();
    if (const BackendModule* registered = factory.find(module.backend)) {
        if (registered->createDevice == module.createDevice)
            return {};
        return std::unexpected(
                makeError(EngineErrorCode::InvalidBackendModule, "A different backend module is already registered"));
    }
    return factory.registerModule(module);
}

Result<std::unique_ptr<RHIDevice>> createRegisteredDevice(const BackendModule& module,
                                                          const DeviceCreateInfo& createInfo) {
    if (auto registered = ensureBackendRegistered(module); !registered)
        return std::unexpected(registered.error());
    return RHIDevice::create(createInfo);
}

class BackendContractTest : public testing::TestWithParam<BackendContractParam> {
protected:
    Result<std::unique_ptr<RHIDevice>> createDevice(ContractWindow& window) const {
        const BackendModule& module = GetParam().module();
        DeviceCreateInfo createInfo;
        createInfo.backend = module.backend;
        createInfo.window = window.nativeHandle();
        createInfo.enableValidation = true;
        createInfo.renderConfig.msaa = RenderConfig::MSAALevel::None;
        return createRegisteredDevice(module, createInfo);
    }
};

#if MULAN_TEST_HAS_RHI_VULKAN && defined(_WIN32)
TEST(VulkanBackendContractTest, OneDeviceAlternatesTwoIndependentSwapChains) {
    ContractWindow firstWindow;
    ContractWindow secondWindow;
    ASSERT_TRUE(firstWindow.valid());
    ASSERT_TRUE(secondWindow.valid());

    DeviceCreateInfo createInfo;
    createInfo.backend = GraphicsBackend::Vulkan;
    createInfo.enableValidation = true;
    createInfo.renderConfig.msaa = RenderConfig::MSAALevel::None;
    auto deviceResult = createRegisteredDevice(vulkanBackendModule(), createInfo);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    auto createSwapChain = [&device](const ContractWindow& window) {
        SwapChainDesc desc;
        desc.window = window.nativeHandle();
        desc.width = 64;
        desc.height = 64;
        desc.bufferCount = 2;
        desc.sampleCount = 1;
        desc.vsync = false;
        return device->createSwapChain(desc);
    };

    auto firstResult = createSwapChain(firstWindow);
    auto secondResult = createSwapChain(secondWindow);
    ASSERT_TRUE(firstResult) << firstResult.error().message;
    ASSERT_TRUE(secondResult) << secondResult.error().message;
    auto first = std::move(*firstResult);
    auto second = std::move(*secondResult);

    auto render = [&device](SwapChain& swapchain) {
        auto commandResult = device->beginFrame(&swapchain);
        ASSERT_TRUE(commandResult) << commandResult.error().message;
        CommandList* command = *commandResult;
        command->beginRenderPass(swapchain.renderPassBeginInfo());
        ASSERT_EQ(command->state(), CommandList::State::Recording)
                << (command->recordingError() ? command->recordingError()->message : "unknown recording error");
        command->endRenderPass();
        auto submission = device->endFrame(&swapchain);
        ASSERT_TRUE(submission) << submission.error().message;
    };

    for (int frame = 0; frame < 8; ++frame) {
        render(*first);
        render(*second);
    }

    ASSERT_TRUE(device->waitIdle());
    first.reset();
    second.reset();
    ASSERT_TRUE(device->waitIdle());
}
#endif

TEST_P(BackendContractTest, CreatesTrackedResourcesAndSubmitsExecutableCommandLists) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);
    EXPECT_EQ(device->backend(), GetParam().module().backend);

    const std::array<std::byte, 64> initialData{};
    auto bufferResult = device->createBuffer(
            BufferDesc::vertex(static_cast<uint32_t>(initialData.size()), initialData.data(), "ContractVertexBuffer"));
    ASSERT_TRUE(bufferResult) << bufferResult.error().message;
    auto buffer = std::move(*bufferResult);
    EXPECT_TRUE(buffer->isTracked());
    EXPECT_TRUE(buffer->belongsTo(*device));

    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin()) << command->recordingError()->message;
    ASSERT_TRUE(command->end()) << command->recordingError()->message;
    EXPECT_EQ(command->state(), CommandList::State::Executable);

    auto submission = device->executeCommandList(command.get());
    ASSERT_TRUE(submission) << submission.error().message;
    EXPECT_TRUE(*submission);
    EXPECT_EQ(command->state(), CommandList::State::Submitted);
    ASSERT_TRUE(device->waitForSubmission(*submission));

    command.reset();
    buffer.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, ReportsUsableCapabilities) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    const GPUDeviceCapabilities& caps = device->capabilities();
    EXPECT_EQ(caps.backend, GetParam().module().backend);
    EXPECT_GT(caps.maxTextureSize, 0u);
    EXPECT_GT(caps.maxSampleCount, 0u);
    EXPECT_GT(caps.minUniformBufferOffsetAlignment, 0u);
    EXPECT_GT(caps.maxUniformBufferBindingSize, 0u);
    EXPECT_FALSE(caps.indirectDispatch && !caps.computeShader);

    if (!caps.computeShader) {
        const ComputePipelineDesc computeDesc;
        const auto compute = device->createComputePipelineState(computeDesc);
        ASSERT_FALSE(compute);
        EXPECT_EQ(compute.error().code, static_cast<int32_t>(EngineErrorCode::BackendNotSupported));
    }

    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, WritesPortableObjectBatchUniformRange) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    constexpr size_t ObjectBatchBytes = 16u * 1024u;
    ASSERT_GE(device->capabilities().maxUniformBufferBindingSize, ObjectBatchBytes);
    std::array<std::byte, ObjectBatchBytes> data{};
    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    const auto slice = command->writeUniformBytes(data);
    ASSERT_TRUE(slice) << slice.error().message;
    EXPECT_EQ(slice->size, ObjectBatchBytes);
    EXPECT_EQ(slice->offset % device->capabilities().minUniformBufferOffsetAlignment, 0u);
    ASSERT_TRUE(command->end()) << command->recordingError()->message;

    auto submission = device->executeCommandList(command.get());
    ASSERT_TRUE(submission) << submission.error().message;
    ASSERT_TRUE(device->waitForSubmission(*submission));
    command.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, UploadsEveryMipSubresourceOfAnOddSizedTexture) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    TextureDesc desc;
    desc.width = 3;
    desc.height = 5;
    desc.mipLevels = 3;
    desc.format = TextureFormat::RGBA8_UNorm;
    desc.usage = TextureUsageFlags::ShaderResource;
    auto textureResult = device->createTexture(desc);
    ASSERT_TRUE(textureResult) << textureResult.error().message;
    auto texture = std::move(*textureResult);

    const std::array<std::pair<uint32_t, uint32_t>, 3> dimensions{ std::pair{ 3u, 5u }, std::pair{ 1u, 2u },
                                                                   std::pair{ 1u, 1u } };
    ASSERT_TRUE(device->beginUploadBatch());
    for (uint32_t mip = 0; mip < dimensions.size(); ++mip) {
        const auto [width, height] = dimensions[mip];
        std::vector<std::byte> pixels(static_cast<size_t>(width) * height * 4u, std::byte{ 0x7f });
        TextureUploadDesc upload = TextureUploadDesc::tightlyPackedBytes(pixels, width, height, desc.format);
        upload.mipLevel = mip;
        ASSERT_TRUE(device->uploadTextureData(texture.get(), upload));
    }
    ASSERT_TRUE(device->flushUploadBatch());
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, BindGroupOwnsLayoutAndRejectsInvalidUniformUpdates) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    const uint32_t alignment = device->capabilities().minUniformBufferOffsetAlignment;
    const uint32_t bufferSize = (std::max) (alignment * 2, 512u);
    auto bufferResult = device->createBuffer(BufferDesc::uniform(bufferSize, "ContractUniformBuffer"));
    ASSERT_TRUE(bufferResult) << bufferResult.error().message;
    auto buffer = std::move(*bufferResult);

    std::unique_ptr<BindGroup> bindGroup;
    uint64_t expectedLayoutHash = 0;
    {
        const std::array entries{ BindGroupLayoutEntry{ 0, 1, DescriptorType::UniformBuffer,
                                                        PipelineBinding::kStageVertex, BindingMode::Static } };
        const BindGroupLayout layout = BindGroupLayout::fromBindings(entries);
        expectedLayoutHash = layout.hash();
        BindGroupDesc desc;
        desc.addUniformBuffer(0, buffer.get(), 0, alignment);
        auto bindGroupResult = device->createBindGroup(layout, desc);
        ASSERT_TRUE(bindGroupResult) << bindGroupResult.error().message;
        bindGroup = std::move(*bindGroupResult);
    }

    EXPECT_EQ(bindGroup->layout().hash(), expectedLayoutHash);
    bindGroup->markClean();
    EXPECT_TRUE(bindGroup->updateUBO(0, buffer.get(), 0, alignment));
    EXPECT_FALSE(bindGroup->dirty());
    EXPECT_FALSE(bindGroup->updateUBO(0, nullptr, 0, alignment));
    EXPECT_FALSE(bindGroup->updateUBO(0, buffer.get(), 1, alignment));
    EXPECT_FALSE(bindGroup->updateUBO(0, buffer.get(), bufferSize, alignment));
    EXPECT_FALSE(bindGroup->dirty());
    EXPECT_TRUE(bindGroup->updateUBO(0, buffer.get(), alignment, alignment));
    EXPECT_TRUE(bindGroup->dirty());
    const uint16_t changedMask = bindGroup->dirtyMask();
    EXPECT_TRUE(bindGroup->updateUBO(0, buffer.get(), alignment, alignment));
    EXPECT_EQ(bindGroup->dirtyMask(), changedMask);

    bindGroup.reset();
    buffer.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, IdenticalTextureAndSamplerUpdatesPreserveDescriptorCleanliness) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    auto makeTexture = [&device](const char* name) {
        TextureDesc desc;
        desc.name = name;
        desc.width = 1;
        desc.height = 1;
        desc.usage = TextureUsageFlags::ShaderResource | TextureUsageFlags::TransferDst;
        return device->createTexture(desc);
    };
    auto firstTextureResult = makeTexture("ContractTextureA");
    auto secondTextureResult = makeTexture("ContractTextureB");
    ASSERT_TRUE(firstTextureResult) << firstTextureResult.error().message;
    ASSERT_TRUE(secondTextureResult) << secondTextureResult.error().message;
    auto firstTexture = std::move(*firstTextureResult);
    auto secondTexture = std::move(*secondTextureResult);

    SamplerDesc samplerDesc;
    auto firstSamplerResult = device->createSampler(samplerDesc);
    auto secondSamplerResult = device->createSampler(samplerDesc);
    ASSERT_TRUE(firstSamplerResult) << firstSamplerResult.error().message;
    ASSERT_TRUE(secondSamplerResult) << secondSamplerResult.error().message;
    auto firstSampler = std::move(*firstSamplerResult);
    auto secondSampler = std::move(*secondSamplerResult);

    const std::array layoutEntries{
        BindGroupLayoutEntry{ 3, 1, DescriptorType::TextureSRV, PipelineBinding::kStageFragment, BindingMode::Static },
        BindGroupLayoutEntry{ 8, 1, DescriptorType::Sampler, PipelineBinding::kStageFragment, BindingMode::Static },
    };
    const BindGroupLayout layout = BindGroupLayout::fromBindings(layoutEntries);
    BindGroupDesc groupDesc;
    groupDesc.addTexture(3, firstTexture.get()).addSampler(8, firstSampler.get());
    auto groupResult = device->createBindGroup(layout, groupDesc);
    ASSERT_TRUE(groupResult) << groupResult.error().message;
    auto group = std::move(*groupResult);

    group->markClean();
    EXPECT_TRUE(group->updateTexture(3, firstTexture.get()));
    EXPECT_TRUE(group->updateSampler(8, firstSampler.get()));
    EXPECT_FALSE(group->dirty());

    EXPECT_TRUE(group->updateTexture(3, secondTexture.get()));
    ASSERT_TRUE(group->dirty());
    const uint16_t textureMask = group->dirtyMask();
    EXPECT_TRUE(group->updateTexture(3, secondTexture.get()));
    EXPECT_EQ(group->dirtyMask(), textureMask);

    EXPECT_TRUE(group->updateSampler(8, secondSampler.get()));
    EXPECT_NE(group->dirtyMask(), textureMask);
    group->markClean();
    EXPECT_FALSE(group->updateTexture(3, nullptr));
    EXPECT_FALSE(group->updateSampler(8, nullptr));
    EXPECT_FALSE(group->dirty());

    group.reset();
    secondSampler.reset();
    firstSampler.reset();
    secondTexture.reset();
    firstTexture.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, RejectsUnsupportedExplicitRenderTargetSamples) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    RenderTargetDesc desc;
    desc.width = 16;
    desc.height = 16;
    desc.sampleCount = device->capabilities().maxSampleCount * 2;
    const auto result = device->createRenderTarget(desc);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, static_cast<int32_t>(EngineErrorCode::RenderTargetCreateFailed));

    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, WritesDynamicBuffersAndRejectsWrongDeviceResources) {
    ContractWindow firstWindow;
    ContractWindow secondWindow;
    ASSERT_TRUE(firstWindow.valid());
    ASSERT_TRUE(secondWindow.valid());
    auto firstResult = createDevice(firstWindow);
    auto secondResult = createDevice(secondWindow);
    ASSERT_TRUE(firstResult) << firstResult.error().message;
    ASSERT_TRUE(secondResult) << secondResult.error().message;
    auto first = std::move(*firstResult);
    auto second = std::move(*secondResult);

    std::array<std::byte, 64> data{};
    auto bufferResult = second->createBuffer(
            BufferDesc::dynamicVertex(static_cast<uint32_t>(data.size()), "ContractDynamicBuffer"));
    ASSERT_TRUE(bufferResult) << bufferResult.error().message;
    auto buffer = std::move(*bufferResult);
    ASSERT_TRUE(buffer->write(0, static_cast<uint32_t>(data.size()), data.data()));

    auto commandResult = first->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    command->setVertexBuffer(0, buffer.get());
    EXPECT_EQ(command->state(), CommandList::State::Invalid);
    EXPECT_FALSE(command->end());

    command.reset();
    buffer.reset();
    ASSERT_TRUE(first->waitIdle());
    ASSERT_TRUE(second->waitIdle());
}

TEST_P(BackendContractTest, WaitIdleCollectsRetiredSubmissions) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    ASSERT_TRUE(command->end());
    auto submission = device->executeCommandList(command.get());
    ASSERT_TRUE(submission) << submission.error().message;

    bool released = false;
    ASSERT_TRUE(device->retire(*submission, [&released] { released = true; }));
    ASSERT_TRUE(device->waitIdle());
    EXPECT_TRUE(released);

    command.reset();
}

TEST_P(BackendContractTest, SafelyDestroysACommandListImmediatelyAfterSubmission) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    ASSERT_TRUE(command->end());
    auto submission = device->executeCommandList(command.get());
    ASSERT_TRUE(submission) << submission.error().message;

    // 显式后端的命令池、描述符池都必须保留到该提交完成。
    command.reset();
    if (device->backend() == GraphicsBackend::D3D12 || device->backend() == GraphicsBackend::Vulkan)
        EXPECT_TRUE(device->isSubmissionComplete(*submission));
    else
        ASSERT_TRUE(device->waitForSubmission(*submission));
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, ResourceDestructionWaitsForSubmittedGpuUse) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);
    const std::array<std::byte, 64> initialData{};
    auto bufferResult = device->createBuffer(
            BufferDesc::vertex(static_cast<uint32_t>(initialData.size()), initialData.data(), "InFlightBuffer"));
    ASSERT_TRUE(bufferResult) << bufferResult.error().message;
    auto buffer = std::move(*bufferResult);
    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    command->setVertexBuffer(0, buffer.get());
    ASSERT_TRUE(command->end()) << command->recordingError()->message;
    auto submission = device->executeCommandList(command.get());
    ASSERT_TRUE(submission) << submission.error().message;

    buffer.reset();

    EXPECT_TRUE(device->isSubmissionComplete(*submission));
    command.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, RejectsResourceDestroyedBetweenRecordingAndSubmission) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);
    const std::array<std::byte, 64> initialData{};
    auto bufferResult = device->createBuffer(
            BufferDesc::vertex(static_cast<uint32_t>(initialData.size()), initialData.data(), "DestroyedBuffer"));
    ASSERT_TRUE(bufferResult) << bufferResult.error().message;
    auto buffer = std::move(*bufferResult);
    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    command->setVertexBuffer(0, buffer.get());
    ASSERT_TRUE(command->end()) << command->recordingError()->message;
    buffer.reset();

    const auto submission = device->executeCommandList(command.get());

    ASSERT_FALSE(submission);
    EXPECT_EQ(submission.error().code, static_cast<int32_t>(EngineErrorCode::SubmissionFailed));
    command.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, RecordsAndSubmitsAnOffscreenRenderPass) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    RenderTargetDesc desc;
    desc.width = 16;
    desc.height = 16;
    desc.sampleCount = 1;
    auto targetResult = device->createRenderTarget(desc);
    ASSERT_TRUE(targetResult) << targetResult.error().message;
    auto target = std::move(*targetResult);

    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    command->beginRenderPass(target->renderPassBeginInfo());
    ASSERT_EQ(command->state(), CommandList::State::Recording)
            << (command->recordingError() ? command->recordingError()->message : "unknown recording error");
    command->endRenderPass();
    ASSERT_TRUE(command->end()) << command->recordingError()->message;
    auto submission = device->executeCommandList(command.get());
    ASSERT_TRUE(submission) << submission.error().message;
    ASSERT_TRUE(device->waitForSubmission(*submission));

    command.reset();
    target.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, InvalidRenderPassCannotBecomeExecutableAndRecordingCanRecover) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());

    RenderPassBeginInfo invalidPass;
    invalidPass.colorCount = RenderPassBeginInfo::kMaxColorTargets + 1;
    invalidPass.width = 1;
    invalidPass.height = 1;
    command->beginRenderPass(invalidPass);
    EXPECT_EQ(command->state(), CommandList::State::Invalid);
    EXPECT_FALSE(command->end());

    EXPECT_TRUE(command->begin());
    EXPECT_TRUE(command->end());
    EXPECT_EQ(command->state(), CommandList::State::Executable);
    command.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, InvalidBufferBindingLatchesTheRecordingError) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);
    ASSERT_TRUE(command->begin());
    command->setVertexBuffer(0, nullptr);
    EXPECT_EQ(command->state(), CommandList::State::Invalid);
    EXPECT_NE(command->recordingError(), nullptr);
    EXPECT_FALSE(command->end());

    EXPECT_TRUE(command->begin());
    EXPECT_TRUE(command->end());
    command.reset();
    ASSERT_TRUE(device->waitIdle());
}

TEST_P(BackendContractTest, ImmutableBufferWriteReturnsAnError) {
    ContractWindow window;
    ASSERT_TRUE(window.valid());
    auto deviceResult = createDevice(window);
    ASSERT_TRUE(deviceResult) << deviceResult.error().message;
    auto device = std::move(*deviceResult);

    const std::array<std::byte, 64> initialData{};
    auto bufferResult = device->createBuffer(
            BufferDesc::vertex(static_cast<uint32_t>(initialData.size()), initialData.data(), "ImmutableBuffer"));
    ASSERT_TRUE(bufferResult) << bufferResult.error().message;
    auto buffer = std::move(*bufferResult);
    const auto write = buffer->write(0, static_cast<uint32_t>(initialData.size()), initialData.data());
    EXPECT_FALSE(write);
    EXPECT_EQ(write.error().code, static_cast<int32_t>(EngineErrorCode::ResourceUploadFailed));
    buffer.reset();
    ASSERT_TRUE(device->waitIdle());
}

INSTANTIATE_TEST_SUITE_P(CompiledRHIBackends, BackendContractTest, testing::ValuesIn(availableBackends()),
                         [](const testing::TestParamInfo<BackendContractParam>& info) {
                             return std::string(info.param.name);
                         });

}  // namespace
}  // namespace mulan::engine
