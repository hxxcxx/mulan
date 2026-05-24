/**
 * @file DX12UploadContext.h
 * @brief D3D12 上传管理器，Staging Buffer 池与 GPU 传输
 * @author hxxcxx
 * @date 2026-04-18
 */
#pragma once

#include "DX12Common.h"

#include <vector>

namespace MulanGeo::engine {

class DX12Buffer;

class DX12UploadContext {
public:
    DX12UploadContext(ID3D12Device* device,
                      ID3D12CommandQueue* queue,
                      uint32_t frameCount);
    ~DX12UploadContext();

    /// 上传 Immutable/Default buffer 的初始数据
    void uploadBuffer(DX12Buffer* dst, const void* data, uint32_t size,
                      uint32_t dstOffset = 0);

    /// 提交并等待所有上传命令完成
    void flush();

private:
    ID3D12Device*         m_device;
    ID3D12CommandQueue*   m_queue;
    ComPtr<ID3D12CommandAllocator> m_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence>   m_fence;
    HANDLE                m_fenceEvent = nullptr;
    uint64_t              m_fenceValue = 0;
    bool                  m_hasCommands = false;

    static constexpr uint32_t kStagingSize = 4 * 1024 * 1024;  // 4MB per slab

    struct StagingSlab {
        ComPtr<ID3D12Resource> resource;
        void* mapped = nullptr;
        uint32_t capacity = 0;
        uint32_t used = 0;
    };

    std::vector<StagingSlab> m_slabs;
    StagingSlab& getOrCreateSlab(uint32_t minSize);
};

} // namespace MulanGeo::Engine
