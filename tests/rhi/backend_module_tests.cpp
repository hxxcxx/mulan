/**
 * @file backend_module_tests.cpp
 * @brief RHI 后端模块显式注册测试
 * @author hxxcxx
 * @date 2026-07-14
 */

#include <mulan/rhi/device_factory.h>
#include <mulan/rhi/engine_error_code.h>

#include <gtest/gtest.h>

namespace mulan::engine {
namespace {

Result<std::unique_ptr<RHIDevice>> rejectDeviceCreation(const DeviceCreateInfo&) {
    return std::unexpected(makeError(EngineErrorCode::BackendNotSupported, "test backend does not create a Device"));
}

TEST(BackendModuleTest, RegistersExplicitModuleAndRejectsDuplicate) {
    auto& factory = DeviceFactory::instance();
    const BackendModule module{ GraphicsBackend::D3D11, "TestD3D11", &rejectDeviceCreation };

    ASSERT_TRUE(factory.registerModule(module));
    const BackendModule* registered = factory.find(GraphicsBackend::D3D11);
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->name, "TestD3D11");
    EXPECT_EQ(registered->createDevice, &rejectDeviceCreation);

    const auto duplicate = factory.registerModule(module);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, static_cast<int32_t>(EngineErrorCode::BackendAlreadyRegistered));
}

TEST(EngineErrorCodeTest, DistinguishesDeviceFailureFromRecoverableOperationFailure) {
    EXPECT_TRUE(isDeviceFatalError(makeError(EngineErrorCode::DeviceLost, "device lost")));
    EXPECT_TRUE(isDeviceFatalError(makeError(EngineErrorCode::SubmissionFailed, "submission failed")));
    EXPECT_TRUE(isDeviceFatalError(makeError(EngineErrorCode::ResourceUploadFailed, "upload failed")));

    EXPECT_FALSE(isDeviceFatalError(makeError(EngineErrorCode::RenderTargetCreateFailed, "target failed")));
    EXPECT_FALSE(isDeviceFatalError(makeError(EngineErrorCode::PipelineCreateFailed, "pipeline failed")));
    EXPECT_FALSE(isDeviceFatalError(Error::make(ErrorCode::InvalidArg, "invalid argument")));
}

}  // namespace
}  // namespace mulan::engine
