/**
 * @file DX11CommandList.h
 * @brief D3D11 命令列表实现（包装 immediate context）
 * @author zmb
 * @date 2026-04-19
 *
 * D3D11 使用 immediate context 直接执行命令（非延迟模式）。
 * 这里包装 ID3D11DeviceContext 实现 CommandList 接口。
 */
#pragma once

#include "../CommandList.h"
#include "DX11Common.h"

namespace MulanGeo::engine
{

class DX11CommandList final : public CommandList
{
public:
    explicit DX11CommandList(ID3D11DeviceContext* ctx);
    ~DX11CommandList() = default;

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

    ID3D11DeviceContext* context() const { return m_ctx; }

private:
    ID3D11DeviceContext* m_ctx;  // not owned — device's immediate context
    uint32_t m_cachedStride = 0;
};

} // namespace MulanGeo::Engine
