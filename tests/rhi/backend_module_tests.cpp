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

std::unique_ptr<RHIDevice> createNullDevice(const DeviceCreateInfo&) {
    return {};
}

TEST(BackendModuleTest, RegistersExplicitModuleAndRejectsDuplicate) {
    auto& factory = DeviceFactory::instance();
    constexpr auto testBackend = static_cast<GraphicsBackend>(0xFF);
    const BackendModule module{ testBackend, "TestNull", &createNullDevice };

    ASSERT_TRUE(factory.registerModule(module));
    const BackendModule* registered = factory.find(testBackend);
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->name, "TestNull");
    EXPECT_EQ(registered->createDevice, &createNullDevice);

    DeviceCreateInfo createInfo;
    createInfo.backend = testBackend;
    const auto created = RHIDevice::create(createInfo);
    ASSERT_FALSE(created);
    EXPECT_EQ(created.error().code, static_cast<int32_t>(EngineErrorCode::DeviceLost));

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
