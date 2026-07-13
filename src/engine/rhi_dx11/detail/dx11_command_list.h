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
#include "dx11_constant_buffer_arena.h"

#include <array>
#include <unordered_map>

namespace mulan::engine {

class DX11Buffer;
class DX11Texture;

class DX11CommandList final : public CommandList {
public:
    DX11CommandList(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11DeviceContext1* ctx1 = nullptr);
    ~DX11CommandList() = default;

    void begin() override;
    void end() override;

    void setPipelineState(PipelineState* pso) override;
    void setViewport(const Viewport& vp) override;
    void setScissorRect(const ScissorRect& rect) override;

    void bindGroup(BindGroup& group) override;
    void bindResources(const BindGroupDesc& group) override;

    void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0) override;
    void setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) override;
    void setIndexBuffer(Buffer* buffer, uint32_t offset = 0, IndexType type = IndexType::UInt32) override;

    void draw(const DrawAttribs& attribs) override;
    void drawIndexed(const DrawIndexedAttribs& attribs) override;
    void drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount = 1, uint32_t stride = 0) override;

    void updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                      ResourceTransitionMode mode = ResourceTransitionMode::Transition) override;

    void transitionResource(Buffer* buffer, ResourceState newState) override;
    void transitionResource(Texture* texture, ResourceState newState) override;
    bool copyTextureToBuffer(Texture* src, Buffer* dst) override;

    void clearColor(float r, float g, float b, float a) override;
    void clearDepth(float depth) override;
    void clearStencil(uint8_t stencil) override;

    // --- RenderPass ---
    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;

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
    void bindTexture(uint32_t slot, Texture* texture, uint32_t stages);
    void bindSampler(uint32_t slot, Sampler* sampler, uint32_t stages);
    bool ensureReadbackTexture(uint32_t width, uint32_t height, DXGI_FORMAT format);
    bool ensureReadbackResolveTexture(uint32_t width, uint32_t height, DXGI_FORMAT format);
    void unbindShaderResources();

    ID3D11Device* m_device = nullptr;        // 非拥有，Device 保证其生命周期
    ID3D11DeviceContext* m_ctx;              // 非拥有，Device 的 immediate context
    ID3D11DeviceContext1* m_ctx1 = nullptr;  // 可选的 D3D11.1 范围绑定接口
    uint32_t m_cachedStride = 0;
    DX11ConstantBufferArena m_constantBufferArena;
    std::unordered_map<ConstantBufferCacheKey, DX11ConstantBufferArena::Allocation, ConstantBufferCacheKeyHash>
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
