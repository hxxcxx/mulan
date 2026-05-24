/**
 * @file DX12CommandList.h
 * @brief D3D12 命令列表实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../CommandList.h"
#include "DX12Common.h"

namespace mulan::engine {

class DX12Texture;  // forward declaration

class DX12CommandList final : public CommandList {
public:
    /// 创建独立的 CommandList（拥有自己的 cmd allocator 和 cmd list）
    DX12CommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator);
    /// 包装已有的 cmd list（帧循环用，不拥有）
    DX12CommandList(ID3D12GraphicsCommandList* existingCmdList);
    ~DX12CommandList();

    void begin() override;
    void end() override;

    void setPipelineState(PipelineState* pso) override;
    void setViewport(const Viewport& vp) override;
    void setScissorRect(const ScissorRect& rect) override;

    void bindResources(const BindGroup& group) override;

    void setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset = 0) override;
    void setVertexBuffers(uint32_t startSlot, uint32_t count,
                          Buffer** buffers, uint32_t* offsets) override;
    void setIndexBuffer(Buffer* buffer, uint32_t offset = 0,
                        IndexType type = IndexType::UInt32) override;

    void draw(const DrawAttribs& attribs) override;
    void drawIndexed(const DrawIndexedAttribs& attribs) override;

    void updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size,
                      const void* data,
                      ResourceTransitionMode mode =
                          ResourceTransitionMode::Transition) override;

    void transitionResource(Buffer* buffer, ResourceState newState) override;
    void transitionResource(Texture* texture, ResourceState newState) override;
    void copyTextureToBuffer(Texture* src, Buffer* dst) override;

    void clearColor(float r, float g, float b, float a) override;
    void clearDepth(float depth) override;
    void clearStencil(uint8_t stencil) override;

    // --- RenderPass ---
    void beginRenderPass(const RenderPassBeginInfo& info) override;
    void endRenderPass() override;

    ID3D12GraphicsCommandList* commandList() const { return m_cmdList.Get(); }

    /// 设置内部命令列表（帧循环中使用外部 cmd list）
    void setCommandList(ID3D12GraphicsCommandList* cmdList);

private:
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    bool m_ownsCmdList = true;  // 是否在析构时释放
    uint32_t m_cachedStride = 0;  // 从 PSO vertexLayout 缓存的 stride
    bool m_rpPresentSource = false; // endRenderPass 中决定 barrier 目标状态（PRESENT vs SRV）
    DX12Texture* m_rpColorTex = nullptr; // 当前 render pass 的颜色附件
};

} // namespace mulan::Engine
