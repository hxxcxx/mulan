#include <gtest/gtest.h>

#include "detail/dx12_buffer.h"
#include "detail/dx12_command_list.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace mulan::engine {
namespace {

struct DX12TestContext {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandAllocator> allocator;

    bool initialize() {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
            FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            return false;
        }
        return SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    }
};

TEST(DX12TransientUniformTest, PreservesIndependentAlignedWrites) {
    DX12TestContext native;
    ASSERT_TRUE(native.initialize());
    auto commandListResult = DX12CommandList::create(native.device.Get(), native.allocator.Get());
    ASSERT_TRUE(commandListResult);
    auto& commandList = **commandListResult;
    commandList.begin();

    const std::array<uint32_t, 4> firstValue{ 1, 2, 3, 4 };
    const std::array<uint32_t, 4> secondValue{ 5, 6, 7, 8 };
    const auto first = commandList.writeUniform(firstValue);
    const auto second = commandList.writeUniform(secondValue);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->offset, 0u);
    EXPECT_EQ(second->offset, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    EXPECT_EQ(first->backingBuffer, second->backingBuffer);

    auto* page = dynamic_cast<DX12Buffer*>(first->backingBuffer);
    ASSERT_NE(page, nullptr);
    ASSERT_NE(page->mappedData(), nullptr);
    std::array<uint32_t, 4> firstRead{};
    std::array<uint32_t, 4> secondRead{};
    std::memcpy(firstRead.data(), static_cast<const std::byte*>(page->mappedData()) + first->offset, sizeof(firstRead));
    std::memcpy(secondRead.data(), static_cast<const std::byte*>(page->mappedData()) + second->offset,
                sizeof(secondRead));
    EXPECT_EQ(firstRead, firstValue);
    EXPECT_EQ(secondRead, secondValue);
}

TEST(DX12TransientUniformTest, RejectsWritesOutsideCommandRecording) {
    DX12TestContext native;
    ASSERT_TRUE(native.initialize());
    auto commandListResult = DX12CommandList::create(native.device.Get(), native.allocator.Get());
    ASSERT_TRUE(commandListResult);
    auto& commandList = **commandListResult;
    const std::array<uint32_t, 4> value{ 1, 2, 3, 4 };

    EXPECT_FALSE(commandList.writeUniform(value));
    commandList.begin();
    EXPECT_TRUE(commandList.writeUniform(value));
    commandList.end();
    EXPECT_FALSE(commandList.writeUniform(value));
}

}  // namespace
}  // namespace mulan::engine
