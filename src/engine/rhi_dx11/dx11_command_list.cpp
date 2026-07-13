#include "detail/dx11_command_list.h"
#include "detail/dx11_bind_group.h"
#include "detail/dx11_buffer.h"
#include "detail/dx11_convert.h"
#include "detail/dx11_pipeline_state.h"
#include "detail/dx11_sampler.h"
#include "detail/dx11_shader.h"
#include "detail/dx11_texture.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {

constexpr uint32_t kConstantBufferRangeAlignment = 256;
constexpr uint32_t kMaximumConstantBufferBytes = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool usesVertexStage(uint32_t stages) {
    return (stages & mulan::engine::PipelineBinding::kStageVertex) != 0;
}

bool usesFragmentStage(uint32_t stages) {
    return (stages & mulan::engine::PipelineBinding::kStageFragment) != 0;
}

// RHI 暂未单独声明 Geometry stage；kStageAll 时按所有图形阶段绑定。
bool usesGeometryStage(uint32_t stages) {
    return stages == mulan::engine::PipelineBinding::kStageAll;
}

}  // namespace

namespace mulan::engine {

DX11CommandList::DX11CommandList(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11DeviceContext1* ctx1)
    : m_device(device), m_ctx(ctx), m_ctx1(ctx1) {
    if (!m_device || !m_ctx)
        throw std::invalid_argument("DX11CommandList requires a valid device and immediate context");
}

void DX11CommandList::begin() {
    // D3D11 immediate context 无需显式 begin。
}

void DX11CommandList::end() {
    // D3D11 immediate context 无需显式 end。
}

void DX11CommandList::setPipelineState(PipelineState* pso) {
    auto* dx11Pso = dynamic_cast<DX11PipelineState*>(pso);
    if (!dx11Pso) {
        LOG_ERROR("[DX11] setPipelineState rejected: pipeline is not a DX11 pipeline");
        return;
    }

    const auto& desc = pso->desc();
    auto* vs = dynamic_cast<DX11Shader*>(desc.vs);
    auto* ps = desc.ps ? dynamic_cast<DX11Shader*>(desc.ps) : nullptr;
    auto* gs = desc.gs ? dynamic_cast<DX11Shader*>(desc.gs) : nullptr;
    if (!vs || (desc.ps && !ps) || (desc.gs && !gs)) {
        LOG_ERROR("[DX11] setPipelineState rejected: pipeline contains a non-DX11 shader");
        return;
    }

    m_ctx->IASetInputLayout(dx11Pso->inputLayout());
    m_ctx->IASetPrimitiveTopology(toDX11Topology(desc.topology));
    m_ctx->VSSetShader(vs->vsShader(), nullptr, 0);
    m_ctx->PSSetShader(ps ? ps->psShader() : nullptr, nullptr, 0);
    m_ctx->GSSetShader(gs ? gs->gsShader() : nullptr, nullptr, 0);

    m_ctx->RSSetState(dx11Pso->rasterizerState());
    const float blendFactor[4] = { 1.f, 1.f, 1.f, 1.f };
    m_ctx->OMSetBlendState(dx11Pso->blendState(), blendFactor, 0xFFFFFFFF);
    m_ctx->OMSetDepthStencilState(dx11Pso->depthStencilState(), 0);

    m_cachedStride = dx11Pso->stride();
}

void DX11CommandList::setViewport(const Viewport& vp) {
    const D3D11_VIEWPORT d3dVp = { vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
    m_ctx->RSSetViewports(1, &d3dVp);
}

void DX11CommandList::setScissorRect(const ScissorRect& rect) {
    const D3D11_RECT d3dRect = { rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
    m_ctx->RSSetScissorRects(1, &d3dRect);
}

void DX11CommandList::bindGroup(BindGroup& group) {
    auto* dx11Group = dynamic_cast<DX11BindGroup*>(&group);
    if (!dx11Group) {
        LOG_ERROR("[DX11] bindGroup rejected: bind group is not a DX11 bind group");
        return;
    }

    bindEntries(dx11Group->entries(), dx11Group->entryCount(), &dx11Group->layout());
    // D3D11 没有 descriptor heap；每次 bind 都会立即写入 context 状态。
    dx11Group->markClean();
}

void DX11CommandList::bindResources(const BindGroupDesc& group) {
    bindEntries(group.entries, group.count, nullptr);
}

void DX11CommandList::bindEntries(const BindGroupEntry* entries, uint8_t count, const BindGroupLayout* layout) {
    if (!entries)
        return;

    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        uint32_t stages = PipelineBinding::kStageVertex | PipelineBinding::kStageFragment;
        const BindGroupLayoutEntry* layoutEntry = nullptr;
        if (layout) {
            const auto& layoutEntries = layout->entries();
            const auto it = std::find_if(layoutEntries.begin(), layoutEntries.end(), [&entry](const auto& candidate) {
                return candidate.binding == entry.binding;
            });
            if (it == layoutEntries.end()) {
                LOG_ERROR("[DX11] bindGroup rejected: binding {} is absent from its layout", entry.binding);
                continue;
            }
            layoutEntry = &*it;
            stages = layoutEntry->stages;
        }

        if (layoutEntry) {
            // 对象化 BindGroup 有明确的布局类型。即使条目暂时为空或更新错误，也要
            // 显式清除对应 D3D11 槽，不能让上一 draw 的状态泄漏到当前 draw。
            switch (layoutEntry->type) {
            case DescriptorType::UniformBuffer: bindConstantBuffer(entry.binding, entry, stages); break;
            case DescriptorType::TextureSRV: bindTexture(entry.binding, entry.texture, stages); break;
            case DescriptorType::Sampler: bindSampler(entry.binding, entry.sampler, stages); break;
            }
        } else if (entry.buffer) {
            bindConstantBuffer(entry.binding, entry, stages);
        } else if (entry.texture) {
            bindTexture(entry.binding, entry.texture, stages);
        } else if (entry.sampler) {
            bindSampler(entry.binding, entry.sampler, stages);
        }
    }
}

bool DX11CommandList::ensureFallbackConstantBuffer(uint32_t slot) {
    if (slot >= m_fallbackConstantBuffers.size() || !m_device)
        return false;
    if (m_fallbackConstantBuffers[slot])
        return true;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = kMaximumConstantBufferBytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    HRESULT hr = m_device->CreateBuffer(&desc, nullptr, &m_fallbackConstantBuffers[slot]);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11Device::CreateBuffer(fallback constant buffer)");
        return false;
    }
    return true;
}

void DX11CommandList::bindConstantBuffer(uint32_t slot, const BindGroupEntry& entry, uint32_t stages) {
    if (slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
        LOG_ERROR("[DX11] bindConstantBuffer rejected: slot {} exceeds the D3D11 limit", slot);
        return;
    }

    auto* buffer = dynamic_cast<DX11Buffer*>(entry.buffer);
    if (!buffer || !buffer->buffer() || !(buffer->bindFlags() & BufferBindFlags::UniformBuffer)) {
        if (entry.buffer)
            LOG_ERROR("[DX11] bindConstantBuffer rejected: binding {} is not a valid DX11 uniform buffer", slot);

        ID3D11Buffer* nullBuffer = nullptr;
        if (m_ctx1) {
            if (usesVertexStage(stages))
                m_ctx1->VSSetConstantBuffers1(slot, 1, &nullBuffer, nullptr, nullptr);
            if (usesFragmentStage(stages))
                m_ctx1->PSSetConstantBuffers1(slot, 1, &nullBuffer, nullptr, nullptr);
            if (usesGeometryStage(stages))
                m_ctx1->GSSetConstantBuffers1(slot, 1, &nullBuffer, nullptr, nullptr);
        } else {
            if (usesVertexStage(stages))
                m_ctx->VSSetConstantBuffers(slot, 1, &nullBuffer);
            if (usesFragmentStage(stages))
                m_ctx->PSSetConstantBuffers(slot, 1, &nullBuffer);
            if (usesGeometryStage(stages))
                m_ctx->GSSetConstantBuffers(slot, 1, &nullBuffer);
        }
        return;
    }
    if ((entry.offset % kConstantBufferRangeAlignment) != 0) {
        LOG_ERROR("[DX11] bindConstantBuffer rejected: offset {} at binding {} is not 256-byte aligned", entry.offset,
                  slot);
        return;
    }
    if (entry.offset >= buffer->size()) {
        LOG_ERROR("[DX11] bindConstantBuffer rejected: offset {} exceeds binding {} buffer size {}", entry.offset, slot,
                  buffer->size());
        return;
    }

    const uint32_t requestedSize = entry.size ? entry.size : buffer->size() - entry.offset;
    if (requestedSize == 0 || requestedSize > buffer->size() - entry.offset) {
        LOG_ERROR("[DX11] bindConstantBuffer rejected: range at binding {} exceeds the source buffer", slot);
        return;
    }
    const uint32_t boundSize = alignUp(requestedSize, kConstantBufferRangeAlignment);
    const uint64_t rangeEnd = static_cast<uint64_t>(entry.offset) + boundSize;
    if (boundSize > kMaximumConstantBufferBytes || rangeEnd > buffer->allocationSize()) {
        LOG_ERROR("[DX11] bindConstantBuffer rejected: range at binding {} exceeds D3D11 limits", slot);
        return;
    }

    ID3D11Buffer* source = buffer->buffer();
    if (m_ctx1) {
        const UINT firstConstant = entry.offset / 16u;
        const UINT constantCount = boundSize / 16u;
        if (usesVertexStage(stages))
            m_ctx1->VSSetConstantBuffers1(slot, 1, &source, &firstConstant, &constantCount);
        if (usesFragmentStage(stages))
            m_ctx1->PSSetConstantBuffers1(slot, 1, &source, &firstConstant, &constantCount);
        if (usesGeometryStage(stages))
            m_ctx1->GSSetConstantBuffers1(slot, 1, &source, &firstConstant, &constantCount);
        return;
    }

    // Windows 7 等没有 DeviceContext1 的系统走兼容路径：先把所需范围复制到
    // 独立的 64KiB 常量缓冲，再从 offset 0 绑定。
    if (!ensureFallbackConstantBuffer(slot))
        return;

    D3D11_BOX sourceBox = {};
    sourceBox.left = entry.offset;
    sourceBox.right = static_cast<UINT>(rangeEnd);
    sourceBox.top = 0;
    sourceBox.bottom = 1;
    sourceBox.front = 0;
    sourceBox.back = 1;

    ID3D11Buffer* fallback = m_fallbackConstantBuffers[slot].Get();
    m_ctx->CopySubresourceRegion(fallback, 0, 0, 0, 0, source, 0, &sourceBox);
    if (usesVertexStage(stages))
        m_ctx->VSSetConstantBuffers(slot, 1, &fallback);
    if (usesFragmentStage(stages))
        m_ctx->PSSetConstantBuffers(slot, 1, &fallback);
    if (usesGeometryStage(stages))
        m_ctx->GSSetConstantBuffers(slot, 1, &fallback);
}

void DX11CommandList::bindTexture(uint32_t slot, Texture* texture, uint32_t stages) {
    if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) {
        LOG_ERROR("[DX11] bindTexture rejected: slot {} exceeds the D3D11 limit", slot);
        return;
    }

    auto* dx11Texture = dynamic_cast<DX11Texture*>(texture);
    if (texture && (!dx11Texture || !dx11Texture->srv()))
        LOG_ERROR("[DX11] bindTexture rejected: binding {} has no valid DX11 SRV", slot);

    ID3D11ShaderResourceView* srv = dx11Texture ? dx11Texture->srv() : nullptr;
    if (usesVertexStage(stages))
        m_ctx->VSSetShaderResources(slot, 1, &srv);
    if (usesFragmentStage(stages))
        m_ctx->PSSetShaderResources(slot, 1, &srv);
    if (usesGeometryStage(stages))
        m_ctx->GSSetShaderResources(slot, 1, &srv);
}

void DX11CommandList::bindSampler(uint32_t slot, Sampler* sampler, uint32_t stages) {
    if (slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
        LOG_ERROR("[DX11] bindSampler rejected: slot {} exceeds the D3D11 limit", slot);
        return;
    }

    auto* dx11Sampler = dynamic_cast<DX11Sampler*>(sampler);
    if (sampler && (!dx11Sampler || !dx11Sampler->handle()))
        LOG_ERROR("[DX11] bindSampler rejected: binding {} has no valid DX11 sampler", slot);

    ID3D11SamplerState* state = dx11Sampler ? dx11Sampler->handle() : nullptr;
    if (usesVertexStage(stages))
        m_ctx->VSSetSamplers(slot, 1, &state);
    if (usesFragmentStage(stages))
        m_ctx->PSSetSamplers(slot, 1, &state);
    if (usesGeometryStage(stages))
        m_ctx->GSSetSamplers(slot, 1, &state);
}

void DX11CommandList::setVertexBuffer(uint32_t slot, Buffer* buffer, uint32_t offset) {
    if (slot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
        LOG_ERROR("[DX11] setVertexBuffer rejected: slot {} exceeds the D3D11 limit", slot);
        return;
    }

    auto* dx11Buffer = dynamic_cast<DX11Buffer*>(buffer);
    if (!dx11Buffer || !dx11Buffer->buffer()) {
        LOG_ERROR("[DX11] setVertexBuffer rejected: invalid DX11 buffer");
        return;
    }
    if (offset >= dx11Buffer->size()) {
        LOG_ERROR("[DX11] setVertexBuffer rejected: offset exceeds the buffer size");
        return;
    }

    ID3D11Buffer* nativeBuffer = dx11Buffer->buffer();
    const UINT stride = m_cachedStride;
    const UINT nativeOffset = offset;
    m_ctx->IASetVertexBuffers(slot, 1, &nativeBuffer, &stride, &nativeOffset);
}

void DX11CommandList::setVertexBuffers(uint32_t startSlot, uint32_t count, Buffer** buffers, uint32_t* offsets) {
    if (!buffers || startSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
        return;
    count = (std::min) (count, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - startSlot);
    if (count == 0)
        return;

    std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> nativeBuffers{};
    std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides{};
    std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> nativeOffsets{};
    for (uint32_t i = 0; i < count; ++i) {
        auto* dx11Buffer = dynamic_cast<DX11Buffer*>(buffers[i]);
        if (!dx11Buffer || !dx11Buffer->buffer()) {
            LOG_ERROR("[DX11] setVertexBuffers rejected: invalid DX11 buffer");
            return;
        }
        const uint32_t offset = offsets ? offsets[i] : 0;
        if (offset >= dx11Buffer->size()) {
            LOG_ERROR("[DX11] setVertexBuffers rejected: offset exceeds the buffer size");
            return;
        }
        nativeBuffers[i] = dx11Buffer->buffer();
        strides[i] = m_cachedStride;
        nativeOffsets[i] = offset;
    }
    m_ctx->IASetVertexBuffers(startSlot, count, nativeBuffers.data(), strides.data(), nativeOffsets.data());
}

void DX11CommandList::setIndexBuffer(Buffer* buffer, uint32_t offset, IndexType type) {
    auto* dx11Buffer = dynamic_cast<DX11Buffer*>(buffer);
    if (!dx11Buffer || !dx11Buffer->buffer() || offset >= dx11Buffer->size()) {
        LOG_ERROR("[DX11] setIndexBuffer rejected: invalid buffer or range");
        return;
    }
    const DXGI_FORMAT format = type == IndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    m_ctx->IASetIndexBuffer(dx11Buffer->buffer(), format, offset);
}

void DX11CommandList::draw(const DrawAttribs& attribs) {
    if (attribs.instanceCount > 1) {
        m_ctx->DrawInstanced(attribs.vertexCount, attribs.instanceCount, attribs.startVertex, attribs.startInstance);
    } else {
        m_ctx->Draw(attribs.vertexCount, attribs.startVertex);
    }
}

void DX11CommandList::drawIndexed(const DrawIndexedAttribs& attribs) {
    if (attribs.instanceCount > 1) {
        m_ctx->DrawIndexedInstanced(attribs.indexCount, attribs.instanceCount, attribs.startIndex, attribs.baseVertex,
                                    attribs.startInstance);
    } else {
        m_ctx->DrawIndexed(attribs.indexCount, attribs.startIndex, attribs.baseVertex);
    }
}

void DX11CommandList::drawIndirect(Buffer* argsBuffer, uint32_t offset, uint32_t drawCount, uint32_t stride) {
    auto* buffer = dynamic_cast<DX11Buffer*>(argsBuffer);
    if (!buffer || !buffer->buffer() || !(buffer->bindFlags() & BufferBindFlags::IndirectBuffer)) {
        LOG_ERROR("[DX11] drawIndirect rejected: a DX11 indirect-argument buffer is required");
        return;
    }

    const uint32_t argumentSize = sizeof(D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS);
    const uint32_t argumentStride = stride ? stride : argumentSize;
    if (argumentStride < argumentSize || drawCount == 0 || offset > buffer->size()) {
        LOG_ERROR("[DX11] drawIndirect rejected: arguments are invalid");
        return;
    }

    for (uint32_t i = 0; i < drawCount; ++i) {
        const uint64_t argumentOffset = static_cast<uint64_t>(offset) + static_cast<uint64_t>(i) * argumentStride;
        if (argumentOffset + argumentSize > buffer->size()) {
            LOG_ERROR("[DX11] drawIndirect rejected: arguments exceed the buffer size");
            return;
        }
        m_ctx->DrawIndexedInstancedIndirect(buffer->buffer(), static_cast<UINT>(argumentOffset));
    }
}

void DX11CommandList::updateBuffer(Buffer* buffer, uint32_t offset, uint32_t size, const void* data,
                                   ResourceTransitionMode) {
    auto* dx11Buffer = dynamic_cast<DX11Buffer*>(buffer);
    if (!dx11Buffer) {
        LOG_ERROR("[DX11] updateBuffer rejected: buffer is not a DX11 buffer");
        return;
    }
    dx11Buffer->update(offset, size, data);
}

void DX11CommandList::transitionResource(Buffer*, ResourceState) {
    // D3D11 自动管理资源状态。
}

void DX11CommandList::transitionResource(Texture*, ResourceState) {
    // D3D11 自动管理资源状态。
}

bool DX11CommandList::ensureReadbackTexture(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (m_readbackTexture && m_readbackWidth == width && m_readbackHeight == height && m_readbackFormat == format)
        return true;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &texture);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11Device::CreateTexture2D(readback staging)");
        return false;
    }
    m_readbackTexture = texture;
    m_readbackWidth = width;
    m_readbackHeight = height;
    m_readbackFormat = format;
    return true;
}

bool DX11CommandList::ensureReadbackResolveTexture(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (m_readbackResolveTexture && m_readbackResolveWidth == width && m_readbackResolveHeight == height &&
        m_readbackResolveFormat == format) {
        return true;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;

    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &texture);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11Device::CreateTexture2D(readback resolve)");
        return false;
    }
    m_readbackResolveTexture = texture;
    m_readbackResolveWidth = width;
    m_readbackResolveHeight = height;
    m_readbackResolveFormat = format;
    return true;
}

bool DX11CommandList::copyTextureToBuffer(Texture* src, Buffer* dst) {
    if (m_renderPassActive) {
        LOG_ERROR("[DX11] copyTextureToBuffer rejected: call it after endRenderPass");
        return false;
    }

    auto* sourceTexture = dynamic_cast<DX11Texture*>(src);
    auto* destinationBuffer = dynamic_cast<DX11Buffer*>(dst);
    if (!sourceTexture || !sourceTexture->resource() || !destinationBuffer || !destinationBuffer->buffer() ||
        destinationBuffer->usage() != BufferUsage::Staging) {
        LOG_ERROR("[DX11] copyTextureToBuffer rejected: a DX11 Texture2D and staging buffer are required");
        return false;
    }

    const auto& textureDesc = sourceTexture->desc();
    const uint32_t bytesPerPixel = textureFormatBytesPerPixel(textureDesc.format);
    const DXGI_FORMAT format = toDXGIFormat11(textureDesc.format);
    if (textureDesc.dimension != TextureDimension::Texture2D || bytesPerPixel == 0 || format == DXGI_FORMAT_UNKNOWN) {
        LOG_ERROR("[DX11] copyTextureToBuffer rejected: only uncompressed color Texture2D resources are supported");
        return false;
    }

    const uint64_t imageSize = static_cast<uint64_t>(textureDesc.width) * textureDesc.height * bytesPerPixel;
    if (imageSize > destinationBuffer->size()) {
        LOG_ERROR("[DX11] copyTextureToBuffer rejected: readback buffer is too small");
        return false;
    }

    // Copy/Resolve 不能与当前 OM 输出绑定重叠；截图路径在 render pass 之后调用，
    // 这里仍主动解绑，避免 D3D11 debug layer 报资源别名。
    m_ctx->OMSetRenderTargets(0, nullptr, nullptr);

    ID3D11Texture2D* copySource = sourceTexture->resource();
    UINT sourceSubresource = D3D11CalcSubresource(0, 0, textureDesc.mipLevels);
    if (textureDesc.sampleCount > 1) {
        if (!ensureReadbackResolveTexture(textureDesc.width, textureDesc.height, format))
            return false;
        m_ctx->ResolveSubresource(m_readbackResolveTexture.Get(), 0, copySource, sourceSubresource, format);
        copySource = m_readbackResolveTexture.Get();
        sourceSubresource = 0;
    }
    if (!ensureReadbackTexture(textureDesc.width, textureDesc.height, format))
        return false;

    m_ctx->CopySubresourceRegion(m_readbackTexture.Get(), 0, 0, 0, 0, copySource, sourceSubresource, nullptr);
    m_ctx->Flush();

    D3D11_MAPPED_SUBRESOURCE sourceMapped = {};
    HRESULT hr = m_ctx->Map(m_readbackTexture.Get(), 0, D3D11_MAP_READ, 0, &sourceMapped);
    if (FAILED(hr)) {
        logDX11Failure(hr, "ID3D11DeviceContext::Map(readback texture)");
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE destinationMapped = {};
    hr = m_ctx->Map(destinationBuffer->buffer(), 0, D3D11_MAP_WRITE, 0, &destinationMapped);
    if (FAILED(hr)) {
        m_ctx->Unmap(m_readbackTexture.Get(), 0);
        logDX11Failure(hr, "ID3D11DeviceContext::Map(readback buffer)");
        return false;
    }

    const uint32_t rowBytes = textureDesc.width * bytesPerPixel;
    auto* destination = static_cast<uint8_t*>(destinationMapped.pData);
    const auto* source = static_cast<const uint8_t*>(sourceMapped.pData);
    for (uint32_t row = 0; row < textureDesc.height; ++row)
        std::memcpy(destination + static_cast<size_t>(row) * rowBytes,
                    source + static_cast<size_t>(row) * sourceMapped.RowPitch, rowBytes);

    m_ctx->Unmap(destinationBuffer->buffer(), 0);
    m_ctx->Unmap(m_readbackTexture.Get(), 0);
    return true;
}

void DX11CommandList::clearColor(float r, float g, float b, float a) {
    const float color[4] = { r, g, b, a };
    for (const auto& attachment : m_activeColorAttachments) {
        if (attachment.target && attachment.target->rtv())
            m_ctx->ClearRenderTargetView(attachment.target->rtv(), color);
    }
}

void DX11CommandList::clearDepth(float depth) {
    if (!m_activeDepthTexture || !m_activeDepthTexture->dsv())
        return;
    m_ctx->ClearDepthStencilView(m_activeDepthTexture->dsv(), D3D11_CLEAR_DEPTH, depth, 0);
}

void DX11CommandList::clearStencil(uint8_t stencil) {
    if (m_activeDepthTexture && m_activeDepthTexture->dsv() && hasStencilFormat11(m_activeDepthTexture->format()))
        m_ctx->ClearDepthStencilView(m_activeDepthTexture->dsv(), D3D11_CLEAR_STENCIL, 1.0f, stencil);
}

void DX11CommandList::unbindShaderResources() {
    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> nullViews{};
    m_ctx->VSSetShaderResources(0, static_cast<UINT>(nullViews.size()), nullViews.data());
    m_ctx->PSSetShaderResources(0, static_cast<UINT>(nullViews.size()), nullViews.data());
    m_ctx->GSSetShaderResources(0, static_cast<UINT>(nullViews.size()), nullViews.data());
}

// ============================================================
// RenderPass
// ============================================================

void DX11CommandList::beginRenderPass(const RenderPassBeginInfo& info) {
    if (m_renderPassActive)
        endRenderPass();

    unbindShaderResources();
    m_activeColorAttachments.fill(ActiveColorAttachment{});
    m_activeDepthTexture = nullptr;
    m_activeDepthStoreAction = StoreAction::Store;

    const auto rejectRenderPass = [this](const char* reason) {
        LOG_ERROR("[DX11] beginRenderPass rejected: {}", reason);
        // 失败后显式解绑，避免后续 draw 意外写入上一轮 render pass 的附件。
        m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        m_activeColorAttachments.fill(ActiveColorAttachment{});
        m_activeDepthTexture = nullptr;
        m_activeDepthStoreAction = StoreAction::Store;
        m_renderPassActive = false;
    };

    if (info.colorCount > RenderPassBeginInfo::kMaxColorTargets) {
        rejectRenderPass("color attachment count exceeds the RHI limit");
        return;
    }

    const uint8_t colorCount = info.colorCount;
    std::array<ID3D11RenderTargetView*, RenderPassBeginInfo::kMaxColorTargets> renderTargetViews{};
    std::array<DX11Texture*, RenderPassBeginInfo::kMaxColorTargets> colorTextures{};
    std::array<DX11Texture*, RenderPassBeginInfo::kMaxColorTargets> resolveTextures{};
    uint32_t attachmentSampleCount = 0;
    for (uint8_t i = 0; i < colorCount; ++i) {
        auto* texture = dynamic_cast<DX11Texture*>(info.colorAttachments[i].target);
        if (!texture || !texture->rtv()) {
            rejectRenderPass("a color attachment has no DX11 RTV");
            return;
        }
        if (attachmentSampleCount == 0) {
            attachmentSampleCount = texture->desc().sampleCount;
        } else if (attachmentSampleCount != texture->desc().sampleCount) {
            rejectRenderPass("all color attachments must use the same sample count");
            return;
        }

        Texture* requestedResolveTarget = info.colorAttachments[i].resolveTarget;
        auto* resolveTarget = dynamic_cast<DX11Texture*>(requestedResolveTarget);
        if (requestedResolveTarget && !resolveTarget) {
            rejectRenderPass("a resolve target belongs to a different RHI backend");
            return;
        }
        if (resolveTarget) {
            if (!resolveTarget->resource() || texture->desc().sampleCount <= 1 ||
                resolveTarget->desc().sampleCount != 1 || resolveTarget->format() != texture->format() ||
                resolveTarget->width() != texture->width() || resolveTarget->height() != texture->height() ||
                resolveTarget->resource() == texture->resource()) {
                rejectRenderPass("a resolve target is incompatible with its multisample color attachment");
                return;
            }
        }

        renderTargetViews[i] = texture->rtv();
        colorTextures[i] = texture;
        resolveTextures[i] = resolveTarget;
    }

    ID3D11DepthStencilView* depthStencilView = nullptr;
    DX11Texture* depthTexture = nullptr;
    if (info.depthAttachment.target) {
        depthTexture = dynamic_cast<DX11Texture*>(info.depthAttachment.target);
        if (!depthTexture || !depthTexture->dsv()) {
            rejectRenderPass("the depth attachment has no DX11 DSV");
            return;
        }
        if (attachmentSampleCount != 0 && attachmentSampleCount != depthTexture->desc().sampleCount) {
            rejectRenderPass("depth and color attachments must use the same sample count");
            return;
        }
        depthStencilView = depthTexture->dsv();
    }

    for (uint8_t i = 0; i < colorCount; ++i) {
        m_activeColorAttachments[i] = { colorTextures[i], resolveTextures[i], info.colorAttachments[i].storeAction };
    }
    m_activeDepthTexture = depthTexture;
    m_activeDepthStoreAction = info.depthAttachment.storeAction;

    m_ctx->OMSetRenderTargets(colorCount, renderTargetViews.data(), depthStencilView);
    m_renderPassActive = true;

    for (uint8_t i = 0; i < colorCount; ++i) {
        const auto& attachment = m_activeColorAttachments[i];
        if (!attachment.target)
            continue;
        if (info.colorAttachments[i].loadAction == LoadAction::Clear) {
            m_ctx->ClearRenderTargetView(attachment.target->rtv(), info.clearColor);
        } else if (info.colorAttachments[i].loadAction == LoadAction::DontCare && m_ctx1) {
            m_ctx1->DiscardView(attachment.target->rtv());
        }
    }

    if (m_activeDepthTexture) {
        if (info.depthAttachment.loadAction == LoadAction::Clear) {
            UINT flags = D3D11_CLEAR_DEPTH;
            if (hasStencilFormat11(m_activeDepthTexture->format()))
                flags |= D3D11_CLEAR_STENCIL;
            m_ctx->ClearDepthStencilView(m_activeDepthTexture->dsv(), flags, info.clearDepth, info.clearStencil);
        } else if (info.depthAttachment.loadAction == LoadAction::DontCare && m_ctx1) {
            m_ctx1->DiscardView(m_activeDepthTexture->dsv());
        }
    }
}

void DX11CommandList::endRenderPass() {
    if (!m_renderPassActive)
        return;

    // Resolve/Discard 与仍作为 OM 输出的资源重叠会触发 debug layer 警告；RenderPass
    // 到此已经结束，先解绑不会影响后续状态。
    m_ctx->OMSetRenderTargets(0, nullptr, nullptr);

    for (const auto& attachment : m_activeColorAttachments) {
        if (!attachment.target)
            continue;

        if (attachment.resolveTarget) {
            if (!attachment.target->resource() || !attachment.resolveTarget->resource() ||
                attachment.target->desc().sampleCount <= 1 || attachment.resolveTarget->desc().sampleCount != 1 ||
                attachment.target->format() != attachment.resolveTarget->format()) {
                LOG_ERROR("[DX11] endRenderPass: invalid multisample resolve attachment");
            } else {
                m_ctx->ResolveSubresource(attachment.resolveTarget->resource(), 0, attachment.target->resource(), 0,
                                          toDXGIFormat11(attachment.target->format()));
            }
        }
        if (attachment.storeAction == StoreAction::DontCare && m_ctx1)
            m_ctx1->DiscardView(attachment.target->rtv());
    }

    if (m_activeDepthTexture && m_activeDepthStoreAction == StoreAction::DontCare && m_ctx1)
        m_ctx1->DiscardView(m_activeDepthTexture->dsv());

    m_activeColorAttachments.fill(ActiveColorAttachment{});
    m_activeDepthTexture = nullptr;
    m_activeDepthStoreAction = StoreAction::Store;
    m_renderPassActive = false;
}

}  // namespace mulan::engine
