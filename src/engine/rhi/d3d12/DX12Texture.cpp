/**
 * @file DX12Texture.cpp
 * @brief D3D12 纹理实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#include "DX12Texture.h"
#include "DX12Convert.h"

namespace mulan::engine {

DX12Texture::DX12Texture(const TextureDesc& desc, ID3D12Device* device,
                         D3D12_RESOURCE_STATES initialState)
    : m_desc(desc)
    , m_state(initialState)
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (desc.usage & TextureUsageFlags::RenderTarget) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (desc.usage & TextureUsageFlags::DepthStencil) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (desc.usage & TextureUsageFlags::UnorderedAccess) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Alignment          = 0;
    resDesc.Width              = desc.width;
    resDesc.Height             = desc.height;
    resDesc.DepthOrArraySize   = static_cast<UINT16>(desc.arraySize);
    resDesc.MipLevels          = static_cast<UINT16>(desc.mipLevels);
    resDesc.Format             = toDXGIFormat(desc.format);
    resDesc.SampleDesc.Count   = desc.sampleCount;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags              = flags;

    D3D12_CLEAR_VALUE clearVal = {};
    D3D12_CLEAR_VALUE* pClearVal = nullptr;
    if (desc.usage & TextureUsageFlags::RenderTarget) {
        clearVal.Format = toDXGIFormat(desc.format);
        memcpy(clearVal.Color, m_desc.format == TextureFormat::Unknown
            ? std::array<float,4>{0.f,0.f,0.f,1.f}.data()
            : std::array<float,4>{0.15f,0.15f,0.15f,1.f}.data(), sizeof(float)*4);
        pClearVal = &clearVal;
    } else if (desc.usage & TextureUsageFlags::DepthStencil) {
        clearVal.Format = toDSVFormat(desc.format);
        clearVal.DepthStencil.Depth = 1.0f;
        clearVal.DepthStencil.Stencil = 0;
        pClearVal = &clearVal;
    }

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &resDesc, initialState,
        pClearVal, IID_PPV_ARGS(&m_resource));
    DX12_CHECK(hr);

    createSRVIfNeeded(device);
}

DX12Texture::DX12Texture(const TextureDesc& desc, ID3D12Resource* existingResource,
                         D3D12_RESOURCE_STATES initialState)
    : m_desc(desc)
    , m_resource(existingResource)
    , m_state(initialState)
{
}

void DX12Texture::createSRVIfNeeded(ID3D12Device* device) {
    if (!(m_desc.usage & TextureUsageFlags::ShaderResource) || !m_resource)
        return;

    // 创建私有 CPU-only 描述符堆存放 SRV
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap));
    if (FAILED(hr)) return;

    m_srv = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = toDXGIFormat(m_desc.format);
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = m_desc.mipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice    = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, m_srv);
    m_hasSRV = true;
}

DX12Texture::~DX12Texture() = default;

} // namespace mulan::engine
