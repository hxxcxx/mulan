#include "detail/dx11_device.h"
#include "../rhi/engine_error_code.h"

#include <d3d11_1.h>
#include <expected>
#include <utility>

namespace mulan::engine {

// ============================================================
// 构造 / 析构
// ============================================================

DX11Device::DX11Device(const DeviceCreateInfo& ci) {
    init(ci);
}

DX11Device::~DX11Device() {
    waitIdle();
    m_frameCmdList.reset();
    m_immediateCtx.Reset();
    m_device.Reset();
    m_factory.Reset();
}

// ============================================================
// 初始化
// ============================================================

void DX11Device::init(const DeviceCreateInfo& ci) {
    m_window = ci.window;
    m_renderConfig = ci.renderConfig;

    // Create DXGI Factory
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
    DX11_CHECK(hr);
    if (FAILED(hr)) {
        return;
    }

    // Device creation flags
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (ci.enableValidation) {
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL achievedLevel = {};

    hr = D3D11CreateDevice(nullptr,  // default adapter
                           D3D_DRIVER_TYPE_HARDWARE,
                           nullptr,  // no software rasterizer
                           createFlags, featureLevels, _countof(featureLevels), D3D11_SDK_VERSION, &m_device,
                           &achievedLevel, &m_immediateCtx);

    // Debug layer is optional: retry without it when unavailable.
    if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, featureLevels,
                               _countof(featureLevels), D3D11_SDK_VERSION, &m_device, &achievedLevel, &m_immediateCtx);
    }

    // Only report error after all retries exhausted.
    DX11_CHECK(hr);

    if (FAILED(hr) || !m_device || !m_immediateCtx) {
        return;
    }

    // Debug layer
    if (ci.enableValidation && (createFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        m_device.As(&m_debugDevice);
    }

    // Frame command list wrapping the immediate context
    m_frameCmdList = std::make_unique<DX11CommandList>(m_immediateCtx.Get());

    // Capabilities
    m_caps.backend = GraphicsBackend::D3D11;
    m_caps.maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    m_caps.maxTextureAniso = D3D11_REQ_MAXANISOTROPY;
    m_caps.depthClamp = true;
    m_caps.geometryShader = true;
    m_caps.tessellationShader = (achievedLevel >= D3D_FEATURE_LEVEL_11_0);
    m_caps.computeShader = (achievedLevel >= D3D_FEATURE_LEVEL_11_0);
}

math::Mat4 DX11Device::clipSpaceCorrectionMatrix() const {
    math::Mat4 mat(1.0);
    mat[1][1] = -1.0;
    mat[2][2] = 0.5;
    mat[3][2] = 0.5;
    return mat;
}

template <typename Base, typename Impl, typename... Args>
static core::Result<std::unique_ptr<Base>> createDX11Resource(EngineErrorCode code, Args&&... args) {
    try {
        return std::unique_ptr<Base>(std::make_unique<Impl>(std::forward<Args>(args)...));
    } catch (const std::exception& e) {
        return std::unexpected(makeError(code, e.what()));
    }
}

core::Result<std::unique_ptr<Buffer>> DX11Device::createBuffer(const BufferDesc& desc) {
    return createDX11Resource<Buffer, DX11Buffer>(EngineErrorCode::BufferCreateFailed, desc, m_device.Get(),
                                                  m_immediateCtx.Get());
}
core::Result<std::unique_ptr<Texture>> DX11Device::createTexture(const TextureDesc& desc) {
    return createDX11Resource<Texture, DX11Texture>(EngineErrorCode::TextureCreateFailed, desc, m_device.Get());
}
core::Result<std::unique_ptr<Shader>> DX11Device::createShader(const ShaderDesc& desc) {
    return createDX11Resource<Shader, DX11Shader>(EngineErrorCode::ShaderCompileFailed, desc, m_device.Get());
}
core::Result<std::unique_ptr<PipelineState>> DX11Device::createPipelineState(const GraphicsPipelineDesc& desc) {
    return createDX11Resource<PipelineState, DX11PipelineState>(EngineErrorCode::PipelineCreateFailed, desc,
                                                                m_device.Get());
}
core::Result<std::unique_ptr<ComputePipelineState>> DX11Device::createComputePipelineState(const ComputePipelineDesc&) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "D3D11 compute pipeline is not implemented"));
}
core::Result<std::unique_ptr<CommandList>> DX11Device::createCommandList() {
    return createDX11Resource<CommandList, DX11CommandList>(EngineErrorCode::CommandListCreateFailed,
                                                            m_immediateCtx.Get());
}
core::Result<std::unique_ptr<SwapChain>> DX11Device::createSwapChain(const SwapChainDesc& desc) {
    if (!desc.window.valid()) {
        return std::unexpected(
                makeError(EngineErrorCode::SwapChainCreateFailed, "DX11 swap chain requires a native window handle"));
    }
    return createDX11Resource<SwapChain, DX11SwapChain>(EngineErrorCode::SwapChainCreateFailed, desc, m_device.Get(),
                                                        m_factory.Get(), m_immediateCtx.Get(), desc.window,
                                                        m_renderConfig);
}
core::Result<std::unique_ptr<RenderTarget>> DX11Device::createRenderTarget(const RenderTargetDesc& desc) {
    return createDX11Resource<RenderTarget, DX11RenderTarget>(EngineErrorCode::RenderTargetCreateFailed, desc,
                                                              m_device.Get());
}
core::Result<std::unique_ptr<Sampler>> DX11Device::createSampler(const SamplerDesc& desc) {
    return createDX11Resource<Sampler, DX11Sampler>(EngineErrorCode::SamplerCreateFailed, desc, m_device.Get());
}
core::Result<std::unique_ptr<Fence>> DX11Device::createFence(uint64_t value) {
    return createDX11Resource<Fence, DX11Fence>(EngineErrorCode::FenceCreateFailed, m_device.Get(), value);
}
core::Result<std::unique_ptr<BindGroup>> DX11Device::createBindGroup(const BindGroupLayout&, const BindGroupDesc&) {
    return std::unexpected(
            makeError(EngineErrorCode::BackendNotSupported, "D3D11 bind-group objects are not implemented"));
}

void DX11Device::uploadTextureData(Texture* dst, const TextureUploadDesc& upload) {
    auto* texture = dynamic_cast<DX11Texture*>(dst);
    const auto bpp = textureFormatBytesPerPixel(upload.format);
    const uint32_t rowPitch = upload.sourceRowPitch ? upload.sourceRowPitch : upload.width * bpp;
    if (texture && !upload.data.empty() && bpp &&
        upload.data.size_bytes() >= static_cast<size_t>(rowPitch) * upload.height)
        m_immediateCtx->UpdateSubresource(texture->resource(), upload.mipLevel, nullptr, upload.data.data(), rowPitch,
                                          upload.sourceSlicePitch);
}

void DX11Device::executeCommandLists(CommandList**, uint32_t, Fence* fence, uint64_t value) {
    if (fence)
        fence->signal(value);
}
void DX11Device::waitIdle() {
    if (!m_immediateCtx)
        return;
    m_immediateCtx->Flush();
}
void DX11Device::beginFrame(SwapChain*) {
}
CommandList* DX11Device::frameCommandList() {
    return m_frameCmdList.get();
}
void DX11Device::submit() {
    if (m_immediateCtx)
        m_immediateCtx->Flush();
}
void DX11Device::present(SwapChain* swapchain) {
    if (swapchain)
        swapchain->present();
}
void DX11Device::submitAndPresent(SwapChain* swapchain) {
    submit();
    present(swapchain);
}
void DX11Device::submitOffscreen() {
    submit();
}

}  // namespace mulan::engine
