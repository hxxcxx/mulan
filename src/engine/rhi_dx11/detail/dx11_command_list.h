/**
 * @file dx11_command_list.h
 * @brief D3D11 命令列表实现（包装 immediate context）
 * @author zmb
 * @date 2026-04-19
 *
 * D3D11 使用 immediate context 直接执行命令（非延迟模式）。
 * 这里包装 ID3D11DeviceContext 实现 CommandList 接口。
 */
#pragma once

#include "../../rhi/command_list.h"
#include "dx11_common.h"
#include "dx11_transient_uniform_arena.h"

#include <array>
#include <unordered_map>

namespace mulan::engine {

class DX11Buffer;
class DX11Texture;

class DX11CommandList final : public CommandList {
public:
    DX11CommandList(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11DeviceContext1* ctx1 = nullptr);
    ~DX11CommandList() = default;

    Result<void> doBegin() override;
    Result<void> doEnd() override;

    void doSetPipelineState(PipelineState* pso) override;
    void doSetComputePipelineState(ComputePipelineState* pso) override;
    void doSetViewport(const Viewport& vp) override;
    void doSetScissorRect(const ScissorRect& rect) override;

    void doBindGroup(BindGroup& group) override;
    void doBindGroup(BindGroup& group, std::span<const DynamicUniformBinding> dynamicUniforms) override;
    Result<UniformSlice> doWriteUniformBytes(std::span<const std::byte> data) override;

    void doSetVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) override;
    void doSetVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) override;
    void doSetIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) override;

    void doDraw(const DrawAttribs& attribs) override;
    void doDrawIndexed(const DrawIndexedAttribs& attribs) override;
    void doDrawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) override;
    void doDispatch(uint32_t threadGroupX, uint32_t threadGroupY, uint32_t threadGroupZ) override;
    void doDispatchIndirect(Buffer* argsBuffer, uint32_t offset) override;
    void doSetPushConstants(uint32_t offset, uint32_t size, const void* data, uint32_t stageFlags) override;

    void doTransitionResource(Texture* texture, ResourceState newState) override;
    Result<void> doCopyTextureToBuffer(Texture* src, Buffer* dst) override;

    // --- RenderPass ---
    Result<void> doBeginRenderPass(const RenderPassBeginInfo& info) override;
    void doEndRenderPass() override;

    ID3D11DeviceContext* context() const { return m_ctx; }
    bool isValid() const { return m_device && m_ctx; }

private:
    struct ConstantBufferCacheKey {
        const DX11Buffer* buffer = nullptr;
        uint64_t version = 0;
        uint32_t offset = 0;
        uint32_t size = 0;

        bool operator==(const ConstantBufferCacheKey&) const = default;
    };

    struct ConstantBufferCacheKeyHash {
        size_t operator()(const ConstantBufferCacheKey& key) const noexcept;
    };

    struct ActiveColorAttachment {
        DX11Texture* target = nullptr;
        DX11Texture* resolveTarget = nullptr;
        StoreAction storeAction = StoreAction::Store;
    };

    void bindEntries(const BindGroupEntry* entries, uint8_t count, const BindGroupLayout* layout);
    void bindConstantBuffer(uint32_t slot, const BindGroupEntry& entry, uint32_t stages);
    void bindUniformSlice(uint32_t slot, const UniformSlice& slice, uint32_t stages);
    void bindTexture(uint32_t slot, Texture* texture, uint32_t stages);
    void bindSampler(uint32_t slot, Sampler* sampler, uint32_t stages);
    bool ensureReadbackTexture(uint32_t width, uint32_t height, DXGI_FORMAT format);
    bool ensureReadbackResolveTexture(uint32_t width, uint32_t height, DXGI_FORMAT format);
    void unbindShaderResources();

    ID3D11Device* m_device = nullptr;        // 非拥有，Device 保证其生命周期
    ID3D11DeviceContext* m_ctx;              // 非拥有，Device 的 immediate context
    ID3D11DeviceContext1* m_ctx1 = nullptr;  // 可选的 D3D11.1 范围绑定接口
    uint32_t m_cachedStride = 0;
    DX11TransientUniformArena m_transientUniformArena;
    std::unordered_map<ConstantBufferCacheKey, DX11TransientUniformArena::Allocation, ConstantBufferCacheKeyHash>
            m_constantBufferCache;
    std::array<ActiveColorAttachment, RenderPassBeginInfo::kMaxColorTargets> m_activeColorAttachments{};
    DX11Texture* m_activeDepthTexture = nullptr;
    StoreAction m_activeDepthStoreAction = StoreAction::Store;
    bool m_renderPassActive = false;

    ComPtr<ID3D11Texture2D> m_readbackTexture;
    ComPtr<ID3D11Texture2D> m_readbackResolveTexture;
    uint32_t m_readbackWidth = 0;
    uint32_t m_readbackHeight = 0;
    DXGI_FORMAT m_readbackFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t m_readbackResolveWidth = 0;
    uint32_t m_readbackResolveHeight = 0;
    DXGI_FORMAT m_readbackResolveFormat = DXGI_FORMAT_UNKNOWN;
};

}  // namespace mulan::engine
