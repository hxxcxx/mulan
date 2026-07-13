#include <gtest/gtest.h>

#include "detail/dx11_bind_group.h"
#include "detail/dx11_buffer.h"
#include "detail/dx11_command_list.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace mulan::engine {
namespace {

struct DX11TestContext {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11DeviceContext1> context1;

    bool initialize() {
        constexpr D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_11_0;
        D3D_FEATURE_LEVEL actualLevel{};
        const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &requestedLevel, 1,
                                             D3D11_SDK_VERSION, &device, &actualLevel, &context);
        if (FAILED(hr))
            return false;
        const HRESULT context1Result = context.As(&context1);
        if (FAILED(context1Result))
            context1.Reset();
        return true;
    }
};

std::array<uint32_t, 4> readUniformSlice(ID3D11Device* device, ID3D11DeviceContext* context,
                                         const UniformSlice& slice) {
    std::array<uint32_t, 4> value{};
    auto* buffer = dynamic_cast<DX11Buffer*>(slice.backingBuffer);
    if (!buffer)
        return value;

    D3D11_BUFFER_DESC sourceDesc{};
    buffer->buffer()->GetDesc(&sourceDesc);
    D3D11_BUFFER_DESC stagingDesc{};
    stagingDesc.ByteWidth = sourceDesc.ByteWidth;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &staging)))
        return value;

    context->CopyResource(staging.Get(), buffer->buffer());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return value;
    std::memcpy(value.data(), static_cast<const std::byte*>(mapped.pData) + slice.offset, sizeof(value));
    context->Unmap(staging.Get(), 0);
    return value;
}

TEST(DX11TransientUniformTest, PreservesIndependentWritesAndBindsTheRequestedSlice) {
    DX11TestContext native;
    ASSERT_TRUE(native.initialize());
    DX11CommandList commandList(native.device.Get(), native.context.Get(), native.context1.Get());
    commandList.begin();

    const std::array<uint32_t, 4> firstValue{ 1, 2, 3, 4 };
    const std::array<uint32_t, 4> secondValue{ 5, 6, 7, 8 };
    const auto first = commandList.writeUniform(firstValue);
    const auto second = commandList.writeUniform(secondValue);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(readUniformSlice(native.device.Get(), native.context.Get(), *first), firstValue);
    EXPECT_EQ(readUniformSlice(native.device.Get(), native.context.Get(), *second), secondValue);

    const std::array layoutEntries{ BindGroupLayoutEntry{ 0, 1, DescriptorType::UniformBuffer,
                                                          PipelineBinding::kStageVertex, BindingMode::Dynamic } };
    const BindGroupLayout layout = BindGroupLayout::fromBindings(layoutEntries);
    const BindGroupDesc desc;
    DX11BindGroup group(layout, desc);
    const std::array bindings{ DynamicUniformBinding{ 0, *second } };
    commandList.bindGroup(group, bindings);

    ComPtr<ID3D11Buffer> bound;
    if (native.context1 && second->offset != 0) {
        UINT firstConstant = 0;
        UINT constantCount = 0;
        native.context1->VSGetConstantBuffers1(0, 1, bound.GetAddressOf(), &firstConstant, &constantCount);
        EXPECT_EQ(firstConstant, second->offset / 16u);
        EXPECT_EQ(constantCount, 16u);
    } else {
        native.context->VSGetConstantBuffers(0, 1, bound.GetAddressOf());
    }
    auto* secondBuffer = dynamic_cast<DX11Buffer*>(second->backingBuffer);
    ASSERT_NE(secondBuffer, nullptr);
    EXPECT_EQ(bound.Get(), secondBuffer->buffer());
}

}  // namespace
}  // namespace mulan::engine
