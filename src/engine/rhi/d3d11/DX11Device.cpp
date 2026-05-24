/**
 * @file DX11Device.cpp
 * @brief D3D11 设备实现
 * @author zmb
 * @date 2026-04-19
 */
#include "DX11Device.h"

#include <d3d11_1.h>

namespace mulan::engine
{

// ============================================================
// 构造 / 析构
// ============================================================

DX11Device::DX11Device(const DeviceCreateInfo& ci)
{
    init(ci);
}

DX11Device::~DX11Device()
{
    waitIdle();
    m_frameCmdList.reset();
    m_immediateCtx.Reset();
    m_device.Reset();
    m_factory.Reset();
}

// ============================================================
// 初始化
// ============================================================

void DX11Device::init(const DeviceCreateInfo& ci)
{
    m_window       = ci.window;
    m_renderConfig = ci.renderConfig;

    // Create DXGI Factory
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
    DX11_CHECK(hr);
    if (FAILED(hr))
    {
        return;
    }

    // Device creation flags
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (ci.enableValidation)
    {
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL achievedLevel = {};

    hr = D3D11CreateDevice(
        nullptr,                    // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // no software rasterizer
        createFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &achievedLevel,
        &m_immediateCtx);

    // Debug layer is optional: retry without it when unavailable.
    if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG))
    {
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            featureLevels,
            _countof(featureLevels),
            D3D11_SDK_VERSION,
            &m_device,
            &achievedLevel,
            &m_immediateCtx);
    }

    // Only report error after all retries exhausted.
    DX11_CHECK(hr);

    if (FAILED(hr) || !m_device || !m_immediateCtx)
    {
        return;
    }

    // Debug layer
    if (ci.enableValidation && (createFlags & D3D11_CREATE_DEVICE_DEBUG))
    {
        m_device.As(&m_debugDevice);
    }

    // Frame command list wrapping the immediate context
    m_frameCmdList = std::make_unique<DX11CommandList>(m_immediateCtx.Get());

    // Capabilities
    m_caps.backend          = GraphicsBackend::D3D11;
    m_caps.maxTextureSize   = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    m_caps.maxTextureAniso  = D3D11_REQ_MAXANISOTROPY;
    m_caps.depthClamp       = true;
    m_caps.geometryShader   = true;
    m_caps.tessellationShader = (achievedLevel >= D3D_FEATURE_LEVEL_11_0);
    m_caps.computeShader    = (achievedLevel >= D3D_FEATURE_LEVEL_11_0);
}

// ============================================================
// Clip Space — D3D11: Y↓ z∈[0,1] (same as D3D12)
// ============================================================

Mat4 DX11Device::clipSpaceCorrectionMatrix() const
{
    // D3D11 NDC: Y↓, z∈[0,1]
    // 投影矩阵产出 z∈[-1,1]，需映射到 [0,1]: z' = 0.5*z + 0.5*w
    // w 必须保持不变，否则透视除法会污染 x/y
    Mat4 mat(1.0);
    mat[1][1] = -1.0;   // Y 翻转
    mat[2][2] =  0.5;   // z scale
    mat[3][2] =  0.5;   // z offset
    return mat;
}

// ============================================================
// 资源创建
// ============================================================

ResourcePtr<Buffer> DX11Device::createBuffer(const BufferDesc& desc)
{
    if (!m_device || !m_immediateCtx) return nullptr;
    return ResourcePtr<Buffer>(new DX11Buffer(desc, m_device.Get(), m_immediateCtx.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Texture> DX11Device::createTexture(const TextureDesc& desc)
{
    if (!m_device) return nullptr;
    return ResourcePtr<Texture>(new DX11Texture(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Shader> DX11Device::createShader(const ShaderDesc& desc)
{
    if (!m_device) return nullptr;
    return ResourcePtr<Shader>(new DX11Shader(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<PipelineState> DX11Device::createPipelineState(const GraphicsPipelineDesc& desc)
{
    if (!m_device) return nullptr;
    return ResourcePtr<PipelineState>(new DX11PipelineState(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<CommandList> DX11Device::createCommandList()
{
    if (!m_immediateCtx) return nullptr;
    return ResourcePtr<CommandList>(new DX11CommandList(m_immediateCtx.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<SwapChain> DX11Device::createSwapChain(const SwapChainDesc& desc)
{
    if (!m_device || !m_factory || !m_immediateCtx) return nullptr;
    return ResourcePtr<SwapChain>(new DX11SwapChain(desc, m_device.Get(), m_factory.Get(),
                             m_immediateCtx.Get(), m_window, m_renderConfig), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<RenderTarget> DX11Device::createRenderTarget(const RenderTargetDesc& desc)
{
    if (!m_device) return nullptr;
    return ResourcePtr<RenderTarget>(new DX11RenderTarget(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Sampler> DX11Device::createSampler(const SamplerDesc& desc)
{
    if (!m_device) return nullptr;
    return ResourcePtr<Sampler>(new DX11Sampler(desc, m_device.Get()), DeviceResourceDeleter{shared_from_this()});
}

ResourcePtr<Fence> DX11Device::createFence(uint64_t initialValue)
{
    if (!m_device) return nullptr;
    return ResourcePtr<Fence>(new DX11Fence(m_device.Get(), initialValue), DeviceResourceDeleter{shared_from_this()});
}

// ============================================================
// 资源销毁
// ============================================================

void DX11Device::destroy(Buffer* r) { delete r; }
void DX11Device::destroy(Texture* r) { delete r; }
void DX11Device::destroy(Shader* r) { delete r; }
void DX11Device::destroy(PipelineState* r) { delete r; }
void DX11Device::destroy(CommandList* r) { delete r; }
void DX11Device::destroy(SwapChain* r) { delete r; }
void DX11Device::destroy(RenderTarget* r) { delete r; }
void DX11Device::destroy(Sampler* r) { delete r; }
void DX11Device::destroy(Fence* r) { delete r; }

// ============================================================
// 命令提交
// ============================================================

void DX11Device::executeCommandLists(CommandList**, uint32_t,
                                      Fence* fence, uint64_t fenceValue)
{
    // D3D11 immediate context: commands are already executed
    // Just signal the fence if provided
    if (fence)
    {
        fence->signal(fenceValue);
    }
}

void DX11Device::waitIdle()
{
    if (!m_immediateCtx) return;
    m_immediateCtx->Flush();

    // Use an event query to wait for GPU idle
    ComPtr<ID3D11Query> query;
    D3D11_QUERY_DESC qd = {};
    qd.Query = D3D11_QUERY_EVENT;
    HRESULT hr = m_device->CreateQuery(&qd, &query);
    if (SUCCEEDED(hr))
    {
        m_immediateCtx->End(query.Get());
        BOOL data = FALSE;
        while (m_immediateCtx->GetData(query.Get(), &data, sizeof(data), 0) == S_FALSE)
        {
            // spin
        }
    }
}

// ============================================================
// 帧循环
// ============================================================

void DX11Device::beginFrame()
{
    // D3D11 immediate mode: nothing to reset per frame
}

CommandList* DX11Device::frameCommandList()
{
    return m_frameCmdList.get();
}

void DX11Device::submitAndPresent(SwapChain* swapchain)
{
    // D3D11: commands already executed on immediate context
    if (swapchain)
    {
        swapchain->present();
    }
}

void DX11Device::submitOffscreen()
{
    // D3D11: commands already executed on immediate context
    if (m_immediateCtx)
    {
        m_immediateCtx->Flush();
    }
}

} // namespace mulan::Engine
