/**
 * @file DX11CommandList.cpp
 * @brief D3D11 命令列表实现
 * @author zmb
 * @date 2026-04-19
 */
#include "DX11CommandList.h"
#include "DX11Buffer.h"
#include "DX11Texture.h"
#include "DX11PipelineState.h"
#include "DX11Shader.h"
#include "DX11Convert.h"

namespace mulan::engine
{

DX11CommandList::DX11CommandList(ID3D11DeviceContext* ctx)
    : m_ctx(ctx)
{
}

void DX11CommandList::begin()
{
    // D3D11 immediate context 无需显式 begin
}

void DX11CommandList::end()
{
    // D3D11 immediate context 无需显式 end
}

void DX11CommandList::setPipelineState(PipelineState* pso)
{
    auto* dx11Pso = static_cast<DX11PipelineState*>(pso);
    const auto& desc = pso->desc();

    // Input Layout
    m_ctx->IASetInputLayout(dx11Pso->inputLayout());
    m_ctx->IASetPrimitiveTopology(toDX11Topology(desc.topology));

    // Shaders
    auto* vs = static_cast<DX11Shader*>(desc.vs);
    auto* ps = static_cast<DX11Shader*>(desc.ps);
    auto* gs = desc.gs ? static_cast<DX11Shader*>(desc.gs) : nullptr;

    m_ctx->VSSetShader(vs ? vs->vsShader() : nullptr, nullptr, 0);
    m_ctx->PSSetShader(ps ? ps->psShader() : nullptr, nullptr, 0);
    m_ctx->GSSetShader(gs ? gs->gsShader() : nullptr, nullptr, 0);

    // Rasterizer / Blend / DepthStencil states
    m_ctx->RSSetState(dx11Pso->rasterizerState());

    float blendFactor[4] = { 1.f, 1.f, 1.f, 1.f };
    m_ctx->OMSetBlendState(dx11Pso->blendState(), blendFactor, 0xFFFFFFFF);
    m_ctx->OMSetDepthStencilState(dx11Pso->depthStencilState(), 0);

    m_cachedStride = dx11Pso->stride();
}

void DX11CommandList::setViewport(const Viewport& vp)
{
    D3D11_VIEWPORT d3dVp = { vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
    m_ctx->RSSetViewports(1, &d3dVp);
}

void DX11CommandList::setScissorRect(const ScissorRect& rect)
{
    D3D11_RECT d3dRect = { rect.x, rect.y,
                           rect.x + rect.width, rect.y + rect.height };
    m_ctx->RSSetScissorRects(1, &d3dRect);
}

void DX11CommandList::bindResources(const BindGroup& group)
{
    for (uint8_t i = 0; i < group.count; ++i)
    {
        const auto& e = group.entries[i];
        if (e.buffer) {
            auto* dx11Buf = static_cast<DX11Buffer*>(e.buffer);
            ID3D11Buffer* buf = dx11Buf->buffer();
            if (!buf) continue;
            uint32_t slot = e.binding;
            m_ctx->VSSetConstantBuffers(slot, 1, &buf);
            m_ctx->PSSetConstantBuffers(slot, 1, &buf);
        }
        // texture → VSSetShaderResources + PSSetShaderResources
    }
}

void DX11CommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset)
{
    auto* dx11Buf = static_cast<DX11Buffer*>(buffer);
    ID3D11Buffer* buf = dx11Buf->buffer();
    UINT stride = m_cachedStride;
    UINT off = offset;
    m_ctx->IASetVertexBuffers(slot, 1, &buf, &stride, &off);
}

void DX11CommandList::setVertexBuffers(uint32_t startSlot, uint32_t count,
                                        Buffer** buffers, uint32_t* offsets)
{
    ID3D11Buffer* bufs[16] = {};
    UINT strides[16] = {};
    UINT offs[16] = {};
    for (uint32_t i = 0; i < count && i < 16; ++i)
    {
        bufs[i]    = static_cast<DX11Buffer*>(buffers[i])->buffer();
        strides[i] = m_cachedStride;
        offs[i]    = offsets ? offsets[i] : 0;
    }
    m_ctx->IASetVertexBuffers(startSlot, count, bufs, strides, offs);
}

void DX11CommandList::setIndexBuffer(Buffer* buffer, uint32_t offset,
                                      IndexType type)
{
    auto* dx11Buf = static_cast<DX11Buffer*>(buffer);
    DXGI_FORMAT fmt = (type == IndexType::UInt16)
                          ? DXGI_FORMAT_R16_UINT
                          : DXGI_FORMAT_R32_UINT;
    m_ctx->IASetIndexBuffer(dx11Buf->buffer(), fmt, offset);
}

void DX11CommandList::draw(const DrawAttribs& attribs)
{
    if (attribs.instanceCount > 1)
    {
        m_ctx->DrawInstanced(attribs.vertexCount, attribs.instanceCount,
                             attribs.startVertex, attribs.startInstance);
    }
    else
    {
        m_ctx->Draw(attribs.vertexCount, attribs.startVertex);
    }
}

void DX11CommandList::drawIndexed(const DrawIndexedAttribs& attribs)
{
    if (attribs.instanceCount > 1)
    {
        m_ctx->DrawIndexedInstanced(attribs.indexCount, attribs.instanceCount,
                                    attribs.startIndex, attribs.baseVertex,
                                    attribs.startInstance);
    }
    else
    {
        m_ctx->DrawIndexed(attribs.indexCount, attribs.startIndex, attribs.baseVertex);
    }
}

void DX11CommandList::updateBuffer(Buffer* buffer, uint32_t offset,
                                    uint32_t size, const void* data,
                                    ResourceTransitionMode)
{
    auto* dx11Buf = static_cast<DX11Buffer*>(buffer);
    dx11Buf->update(offset, size, data);
}

void DX11CommandList::transitionResource(Buffer*, ResourceState)
{
    // D3D11 无需显式资源状态转换
}

void DX11CommandList::transitionResource(Texture*, ResourceState)
{
    // D3D11 无需显式资源状态转换
}

void DX11CommandList::copyTextureToBuffer(Texture* src, Buffer* dst)
{
    auto* dx11Tex = static_cast<DX11Texture*>(src);
    auto* dx11Buf = static_cast<DX11Buffer*>(dst);
    m_ctx->CopyResource(dx11Buf->buffer(), dx11Tex->resource());
}

void DX11CommandList::clearColor(float r, float g, float b, float a)
{
    // 由 SwapChain/RenderTarget 在 beginRenderPass 中处理
    (void)r; (void)g; (void)b; (void)a;
}

void DX11CommandList::clearDepth(float depth)
{
    (void)depth;
}

void DX11CommandList::clearStencil(uint8_t stencil)
{
    (void)stencil;
}

// ============================================================
// RenderPass
// ============================================================

void DX11CommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    // D3D11: OMSetRenderTargets + Clear

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;

    if (info.colorCount > 0 && info.colorAttachments[0].target) {
        auto* tex = static_cast<DX11Texture*>(info.colorAttachments[0].target);
        rtv = tex->rtv();

        if (info.colorAttachments[0].loadAction == LoadAction::Clear) {
            m_ctx->ClearRenderTargetView(rtv, info.clearColor);
        }
    }

    if (info.depthAttachment.target) {
        auto* depthTex = static_cast<DX11Texture*>(info.depthAttachment.target);
        dsv = depthTex->dsv();

        if (info.depthAttachment.loadAction == LoadAction::Clear) {
            m_ctx->ClearDepthStencilView(dsv,
                D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                info.clearDepth, info.clearStencil);
        }
    }

    m_ctx->OMSetRenderTargets(1, &rtv, dsv);
}

void DX11CommandList::endRenderPass() {
    // D3D11: no explicit end — render targets remain bound until changed
}

} // namespace mulan::Engine
