#include <gtest/gtest.h>

#include "detail/dx12_buffer.h"
#include "detail/dx12_command_list.h"
#include "detail/dx12_descriptor_allocator.h"
#include "detail/dx12_texture.h"

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
    ASSERT_TRUE(commandList.begin());

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
    ASSERT_TRUE(commandList.begin());
    EXPECT_EQ(commandList.state(), CommandList::State::Recording);
    EXPECT_TRUE(commandList.writeUniform(value));
    ASSERT_TRUE(commandList.end());
    EXPECT_EQ(commandList.state(), CommandList::State::Executable);
    EXPECT_FALSE(commandList.writeUniform(value));
}

TEST(DX12DescriptorAllocatorTest, RejectsExhaustionWithoutReturningOutOfRangeHandles) {
    DX12TestContext native;
    ASSERT_TRUE(native.initialize());
    DX12DescriptorAllocator allocator(native.device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                      D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 2);
    ASSERT_TRUE(allocator.isValid());

    EXPECT_NE(allocator.allocate().gpu.ptr, 0u);
    EXPECT_NE(allocator.allocate().gpu.ptr, 0u);
    const DX12Descriptor exhausted = allocator.allocate();
    EXPECT_EQ(exhausted.cpu.ptr, 0u);
    EXPECT_EQ(exhausted.gpu.ptr, 0u);
    EXPECT_EQ(allocator.allocatedCount(), 2u);
}

TEST(DX12DescriptorAllocatorTest, StandaloneCommandListsUseIndependentShaderVisibleHeaps) {
    DX12TestContext native;
    ASSERT_TRUE(native.initialize());
    ComPtr<ID3D12CommandAllocator> secondAllocator;
    ASSERT_TRUE(SUCCEEDED(
            native.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&secondAllocator))));
    auto firstResult = DX12CommandList::create(native.device.Get(), native.allocator.Get());
    auto secondResult = DX12CommandList::create(native.device.Get(), secondAllocator.Get());
    ASSERT_TRUE(firstResult);
    ASSERT_TRUE(secondResult);

    auto firstArena = std::make_unique<DX12DescriptorAllocator>(
            native.device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 8);
    auto secondArena = std::make_unique<DX12DescriptorAllocator>(
            native.device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 8);
    (*firstResult)->setOwnedDescriptorArena(std::move(firstArena));
    (*secondResult)->setOwnedDescriptorArena(std::move(secondArena));

    ASSERT_NE((*firstResult)->descriptorHeap(), nullptr);
    ASSERT_NE((*secondResult)->descriptorHeap(), nullptr);
    EXPECT_NE((*firstResult)->descriptorHeap(), (*secondResult)->descriptorHeap());
}

TEST(DX12CommandStateTest, AbandonedRecordingDoesNotMutateTextureGlobalState) {
    DX12TestContext native;
    ASSERT_TRUE(native.initialize());
    auto commandListResult = DX12CommandList::create(native.device.Get(), native.allocator.Get());
    ASSERT_TRUE(commandListResult);
    auto textureResult = DX12Texture::create(
            TextureDesc::renderTarget(4, 4, TextureFormat::RGBA8_UNorm, "StateTrackingTexture"), native.device.Get());
    ASSERT_TRUE(textureResult);

    auto& commandList = **commandListResult;
    auto& texture = **textureResult;
    const D3D12_RESOURCE_STATES initialState = texture.state();
    ASSERT_TRUE(commandList.begin());
    commandList.transitionResource(&texture, ResourceState::CopySrc);
    EXPECT_EQ(commandList.state(), CommandList::State::Recording);
    EXPECT_EQ(texture.state(), initialState);
    ASSERT_TRUE(commandList.end());
    EXPECT_EQ(texture.state(), initialState);
}

}  // namespace
}  // namespace mulan::engine
