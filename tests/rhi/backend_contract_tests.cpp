/**
 * @file backend_contract_tests.cpp
 * @brief 四种 RHI 后端共享的设备与命令录制契约测试
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/rhi/device_factory.h>

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

class BackendContractTest : public testing::TestWithParam<BackendContractParam> {
protected:
    core::Result<std::unique_ptr<RHIDevice>> createDevice(ContractWindow& window) const {
        const BackendModule& module = GetParam().module();
        DeviceCreateInfo createInfo;
        createInfo.backend = module.backend;
        createInfo.window = window.nativeHandle();
        createInfo.enableValidation = true;
        createInfo.renderConfig.msaa = RenderConfig::MSAALevel::None;
        return module.createDevice(createInfo);
    }
};

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

TEST_P(BackendContractTest, ImmutableBufferUpdateLatchesTheRecordingError) {
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
    auto commandResult = device->createCommandList();
    ASSERT_TRUE(commandResult) << commandResult.error().message;
    auto command = std::move(*commandResult);

    ASSERT_TRUE(command->begin());
    command->updateBuffer(buffer.get(), 0, static_cast<uint32_t>(initialData.size()), initialData.data());
    EXPECT_EQ(command->state(), CommandList::State::Invalid);
    EXPECT_NE(command->recordingError(), nullptr);
    EXPECT_FALSE(command->end());

    EXPECT_TRUE(command->begin());
    EXPECT_TRUE(command->end());
    command.reset();
    buffer.reset();
    ASSERT_TRUE(device->waitIdle());
}

INSTANTIATE_TEST_SUITE_P(CompiledRHIBackends, BackendContractTest, testing::ValuesIn(availableBackends()),
                         [](const testing::TestParamInfo<BackendContractParam>& info) {
                             return std::string(info.param.name);
                         });

}  // namespace
}  // namespace mulan::engine
