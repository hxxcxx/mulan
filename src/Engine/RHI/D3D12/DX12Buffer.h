/**
 * @file DX12Buffer.h
 * @brief D3D12 缓冲区实现
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "../Buffer.h"
#include "DX12Common.h"

#include <vector>

namespace MulanGeo::engine {

class DX12Buffer final : public Buffer {
public:
    DX12Buffer(const BufferDesc& desc, ID3D12Device* device);
    ~DX12Buffer();

    const BufferDesc& desc() const override { return m_desc; }
    void update(uint32_t offset, uint32_t size, const void* data) override;
    bool readback(uint32_t offset, uint32_t size, void* outData) override;

    ID3D12Resource* resource() const { return m_resource.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress() const { return m_resource->GetGPUVirtualAddress(); }
    void* mappedData() const { return m_mappedData; }
    uint64_t uploadFenceValue() const { return m_uploadFenceValue; }
    void setUploadFenceValue(uint64_t v) { m_uploadFenceValue = v; }

    const void* pendingData() const { return m_pendingData.data(); }
    bool needsUpload() const { return !m_pendingData.empty(); }
    void markUploaded() { m_pendingData.clear(); m_pendingData.shrink_to_fit(); }

private:
    BufferDesc           m_desc;
    ComPtr<ID3D12Resource> m_resource;
    void*                m_mappedData = nullptr;
    uint64_t             m_uploadFenceValue = 0;
    std::vector<uint8_t> m_pendingData;  // Immutable buffer 的待上传数据
};

} // namespace MulanGeo::Engine
